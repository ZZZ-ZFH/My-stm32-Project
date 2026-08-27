#include "dx24.h"
#include "stdio.h"

/*
蓝牙模块
U2_TX  PA2
U2_RX  PA3
*/
void DX24_Init(u32 baud)
{
    GPIO_InitTypeDef GPIO_InitStruct;
    USART_InitTypeDef USART_InitStruct;
    NVIC_InitTypeDef NVIC_InitStruct;
    
    //1.时钟使能
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    
    //2.GPIO初始化
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_2|GPIO_Pin_3;//引脚
    GPIO_InitStruct.GPIO_Mode= GPIO_Mode_AF;//模式  复用
    GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;//推挽
    GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_UP;//上拉
    GPIO_InitStruct.GPIO_Speed = GPIO_Medium_Speed;//速度
    
    GPIO_Init(GPIOA,&GPIO_InitStruct);
    
    //3.UART初始化
    USART_InitStruct.USART_BaudRate = baud; //波特率
    USART_InitStruct.USART_WordLength = USART_WordLength_8b; //字长
    USART_InitStruct.USART_StopBits = USART_StopBits_1; //停止位
    USART_InitStruct.USART_Parity = USART_Parity_No ; // 无校验位
    USART_InitStruct.USART_Mode = USART_Mode_Rx | USART_Mode_Tx; //能发送能接收
    USART_InitStruct.USART_HardwareFlowControl = USART_HardwareFlowControl_None;   //无硬件流
    USART_Init(USART2,&USART_InitStruct);
    
    //4.UART与GPIO连接在一起--映射
    GPIO_PinAFConfig(GPIOA,GPIO_PinSource2,GPIO_AF_USART2);
    GPIO_PinAFConfig(GPIOA,GPIO_PinSource3,GPIO_AF_USART2);
    
    //5.配置NVIC--接收中断
    NVIC_InitStruct.NVIC_IRQChannel = USART2_IRQn ;//中断源 中断通道 stm32f4xx.h
    NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;//使能
    NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 1;//抢占优先级
    NVIC_InitStruct.NVIC_IRQChannelSubPriority  = 1;//响应优先级
    NVIC_Init(&NVIC_InitStruct);
    
    //6.接收中断使能
    USART_ITConfig(USART2,USART_IT_RXNE,ENABLE);
	//空闲中断使能IDLE
	USART_ITConfig(USART2,USART_IT_IDLE,ENABLE);
	
    //7.UART使能
    USART_Cmd(USART2,ENABLE);
}
#if 0
//8.编写串口接收中断服务函数
void USART2_IRQHandler()
{
    //1.判断接收中断标志位是否触发
    if(USART_GetITStatus(USART2, USART_IT_RXNE) == SET)
    {
        //接收数据
        uint16_t data = USART_ReceiveData(USART2);
		
		rx_buf[rx_index++] = data;
		
        //回射
        USART_SendData(USART2,data);
        
        USART_ClearITPendingBit(USART2,USART_IT_RXNE);//清空接收中断标志位
    } 
	//判断空闲中断
    if(USART_GetITStatus(USART2, USART_IT_IDLE) == SET)
    {
		rx_flag = 1;
		/* 手册26.6.1USART寄存器描述，先读SR再读DR，清除IDLE标志位*/
        USART2->SR;
        USART2->DR;
    } 
}
#endif
void DX24_SendStr(uint8_t *str,u16 len)
{
	for(int i=0; i<len; i++)
	{
		//发送缓冲区为空
		while(!USART_GetFlagStatus(USART2, USART_FLAG_TXE));
		USART_SendData(USART2,str[i]);
	}
}

void DX24_Demo()
{
	while(1){
		if(rx_flag==1)
		{
			//接收到数据之后的处理
			printf("rx_buf:%s\r\n",rx_buf);
			
			if(strcmp((char*)rx_buf,"LED00")==0)
			{
				//LED0(0);
			}
			memset(rx_buf,0,sizeof(rx_buf));
			rx_flag = 0;
			rx_index = 0;
		}
		
		delay_s(1);
	}
}
