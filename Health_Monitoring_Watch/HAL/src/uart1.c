#include "uart1.h"
#include "stdio.h"

/* USART1 接收缓冲(中断写入, protocol.c 解析) */
uint8_t rx_buf[MAX_FRAME_LEN] = {0};
u16     rx_index = 0;
uint8_t rx_flag = 0;


#pragma import(__use_no_semihosting)             
//标准库需要的支持函数                 
struct __FILE 
{ 
    int handle; 
}; 

FILE __stdout;       
//定义_sys_exit()以避免使用半主机模式    
int _sys_exit(int x) 
{ 
    x = x; 
} 
//重定义fputc函数   printf 是一个宏
int fputc(int ch, FILE *f)
{     
    USART_SendData(USART1,ch);  //通过串口发送数据
    //等待数据发送完毕
    while(USART_GetFlagStatus(USART1,USART_FLAG_TXE)==RESET);      
    return ch;
}


/*
U1_TX  PA9
U1_RX  PA10
*/
void Uart1_Init(u32 baud)
{
    GPIO_InitTypeDef GPIO_InitStruct;
    USART_InitTypeDef USART_InitStruct;
    NVIC_InitTypeDef NVIC_InitStruct;
    
    //1.时钟使能
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    
    //2.GPIO初始化
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_9|GPIO_Pin_10;//引脚
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
    USART_Init(USART1,&USART_InitStruct);
    
    //4.UART与GPIO连接在一起--映射
    GPIO_PinAFConfig(GPIOA,GPIO_PinSource9,GPIO_AF_USART1);
    GPIO_PinAFConfig(GPIOA,GPIO_PinSource10,GPIO_AF_USART1);
    
    //5.配置NVIC--接收中断
    NVIC_InitStruct.NVIC_IRQChannel = USART1_IRQn ;//中断源 中断通道 stm32f4xx.h
    NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;//使能
    NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 7;//抢占优先级
    NVIC_InitStruct.NVIC_IRQChannelSubPriority  = 0;//响应优先级
    NVIC_Init(&NVIC_InitStruct);
    
    //6.接收中断使能
    USART_ITConfig(USART1,USART_IT_RXNE,ENABLE);
	//空闲中断使能IDLE
	USART_ITConfig(USART1,USART_IT_IDLE,ENABLE);
	
    //7.UART使能
    USART_Cmd(USART1,ENABLE);
}

//8.编写串口接收中断服务函数
void USART1_IRQHandler()
{
    //1.判断接收中断标志位是否触发
    if(USART_GetITStatus(USART1, USART_IT_RXNE) == SET)
    {
        //接收数据
        uint16_t data = USART_ReceiveData(USART1);
		
		rx_buf[rx_index++] = data;
		
//		if(rx_buf[rx_index - 1-1]==FRAME_TAIL1 &&  rx_buf[rx_index - 1] == FRAME_TAIL2)
//		{
//			rx_flag = 1;
//		}
		
        //回射
        //USART_SendData(USART1,data);
        
        USART_ClearITPendingBit(USART1,USART_IT_RXNE);//清空接收中断标志位
    } 
	//判断空闲中断
    if(USART_GetITStatus(USART1, USART_IT_IDLE) == SET)
    {
		rx_flag = 1;
		/* 手册26.6.1USART寄存器描述，先读SR再读DR，清除IDLE标志位*/
        USART1->SR;
        USART1->DR;
    } 
}

void Uart1_SendStr(uint8_t *str,u16 len)
{
	for(int i=0; i<len; i++)
	{
		//发送缓冲区为空
		while(!USART_GetFlagStatus(USART1, USART_FLAG_TXE));
		USART_SendData(USART1,str[i]);
	}
}
