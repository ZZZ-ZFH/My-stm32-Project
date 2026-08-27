#include "beep.h"

/*
BEEP   PF8  高电平响  低电平不响
*/
void Beep_Init()
{
	GPIO_InitTypeDef GPIO_InitStruct;
	
	/**********PF8***************/
	//1.GPIOF组时钟使能
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOF, ENABLE);
	//2.GPIO配置
	GPIO_InitStruct.GPIO_Pin   = GPIO_Pin_8;//引脚
	GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_OUT;//输出模式
	GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;//推挽
	GPIO_InitStruct.GPIO_PuPd  = GPIO_PuPd_NOPULL;//浮空
	GPIO_InitStruct.GPIO_Speed = GPIO_Medium_Speed;//速度
	GPIO_Init(GPIOF,&GPIO_InitStruct);
}
void Beep_Demo()
{
	while(1)
	{
		BEEP(1);
		delay_ms(100);
		BEEP(0);
		delay_ms(2000);
	}
}