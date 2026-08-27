/**
 * @file    dx24.c
 * @brief   DX24蓝牙模块驱动: USART6 (PC6-TX, PC7-RX)
 *          接收: RXNE逐字节入缓冲, IDLE标志一帧结束并通知任务
 *          发送: 查询TXE阻塞发送
 */
#include "dx24.h"

/*
蓝牙模块接线:
U6_TX  PC6  --> 蓝牙RXD
U6_RX  PC7  <-- 蓝牙TXD
STATE  PB6  --> 连接状态(手机连接后为高电平)
(PC7原为MAX30102 INT, 已移至PC11/EXTI11)
*/

/* 接收缓冲(中断写入, 任务读取后清零) */
uint8_t dx_rx_buf[DX24_RX_BUF_LEN] = {0};
volatile u16     dx_rx_len  = 0;   /* 本帧长度, IDLE中断时更新 */
volatile uint8_t dx_rx_flag = 0;   /* 1=收到一帧完整数据 */

/* 当前帧写入位置(ISR内部使用, 末字节保留给'\0'终止符) */
static u16 s_rx_widx = 0;

/* 接收事件通知任务句柄, 由DX24_SetNotifyTask在Init前注册 */
static TaskHandle_t s_notify_task = NULL;

void DX24_Init(u32 baud)
{
    GPIO_InitTypeDef GPIO_InitStruct;
    USART_InitTypeDef USART_InitStruct;
    NVIC_InitTypeDef NVIC_InitStruct;

    //1.时钟使能 (USART6挂在APB2总线)
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART6, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);   // STATE脚PB6

    //2.GPIO初始化
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_6|GPIO_Pin_7;//引脚
    GPIO_InitStruct.GPIO_Mode= GPIO_Mode_AF;//模式  复用
    GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;//推挽
    GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_UP;//上拉
    GPIO_InitStruct.GPIO_Speed = GPIO_Medium_Speed;//速度

    GPIO_Init(GPIOC,&GPIO_InitStruct);

    //2.1 STATE引脚初始化: 输入下拉(悬空时为断开态, 防误报已连接)
    GPIO_InitStruct.GPIO_Pin  = DX24_STATE_PIN;//引脚
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IN;//模式  输入
    GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_DOWN;//下拉
    GPIO_Init(DX24_STATE_PORT,&GPIO_InitStruct);

    //3.UART初始化
    USART_InitStruct.USART_BaudRate = baud; //波特率
    USART_InitStruct.USART_WordLength = USART_WordLength_8b; //字长
    USART_InitStruct.USART_StopBits = USART_StopBits_1; //停止位
    USART_InitStruct.USART_Parity = USART_Parity_No ; // 无校验位
    USART_InitStruct.USART_Mode = USART_Mode_Rx | USART_Mode_Tx; //能发送能接收
    USART_InitStruct.USART_HardwareFlowControl = USART_HardwareFlowControl_None;   //无硬件流

    USART_Init(USART6,&USART_InitStruct);

    //4.UART与GPIO连接在一起--映射
    GPIO_PinAFConfig(GPIOC,GPIO_PinSource6,GPIO_AF_USART6);
    GPIO_PinAFConfig(GPIOC,GPIO_PinSource7,GPIO_AF_USART6);

    //5.配置NVIC--接收中断
    //优先级6: 数值须>=FreeRTOS临界值5, 否则FromISR API不允许
    NVIC_InitStruct.NVIC_IRQChannel = USART6_IRQn ;//中断源 中断通道
    NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;//使能
    NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 6;//抢占优先级
    NVIC_InitStruct.NVIC_IRQChannelSubPriority  = 0;//响应优先级
    NVIC_Init(&NVIC_InitStruct);

    //6.接收中断使能
    USART_ITConfig(USART6,USART_IT_RXNE,ENABLE);
	//空闲中断使能IDLE (一帧结束标志)
	USART_ITConfig(USART6,USART_IT_IDLE,ENABLE);

    //7.UART使能
    USART_Cmd(USART6,ENABLE);
}

void DX24_SetNotifyTask(TaskHandle_t task)
{
    /* 须在DX24_Init(使能中断)之前注册, 避免ISR访问竞态 */
    s_notify_task = task;
}

/* 读STATE脚: 高电平=手机已连接蓝牙 */
uint8_t DX24_IsConnected(void)
{
    return (GPIO_ReadInputDataBit(DX24_STATE_PORT, DX24_STATE_PIN)
            == Bit_SET) ? 1 : 0;
}

//8.编写串口接收中断服务函数
void USART6_IRQHandler(void)
{
    BaseType_t higher_woken = pdFALSE;

    //1.判断接收中断标志位是否触发
    if(USART_GetITStatus(USART6, USART_IT_RXNE) == SET)
    {
        //接收数据
        uint16_t data = USART_ReceiveData(USART6);

        //缓冲区溢出保护: 预留末字节给'\0', 满则丢弃该字节
        if(s_rx_widx < DX24_RX_BUF_LEN-1)
        {
            dx_rx_buf[s_rx_widx++] = (uint8_t)data;
        }

        USART_ClearITPendingBit(USART6,USART_IT_RXNE);//清空接收中断标志位
    }
	//判断空闲中断: 一帧数据接收完成
    if(USART_GetITStatus(USART6, USART_IT_IDLE) == SET)
    {
        if(s_rx_widx > 0)
        {
            dx_rx_len  = s_rx_widx;   /* 缓冲区初始化已为全0, 天然带'\0' */
            dx_rx_flag = 1;
            s_rx_widx  = 0;

            /* 通知任务处理本帧数据 */
            if(s_notify_task != NULL)
            {
                vTaskNotifyGiveFromISR(s_notify_task, &higher_woken);
                portYIELD_FROM_ISR(higher_woken);
            }
        }
		/* 手册26.6.1USART寄存器描述，先读SR再读DR，清除IDLE标志位*/
        USART6->SR;
        USART6->DR;
    }
}

void DX24_SendStr(uint8_t *str,u16 len)
{
	for(int i=0; i<len; i++)
	{
		//发送缓冲区为空
		while(!USART_GetFlagStatus(USART6, USART_FLAG_TXE));
		USART_SendData(USART6,str[i]);
	}
}
