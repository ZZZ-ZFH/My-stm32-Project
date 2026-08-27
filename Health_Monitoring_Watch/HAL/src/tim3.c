#include "tim3.h"

#include "lvgl.h"

void TIM3_Init(void)
{
    /* 开启时钟*/
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3,ENABLE);
    
    /* 配置定时器*/
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStruct;
    TIM_TimeBaseInitStruct.TIM_Period = 1000-1;
    TIM_TimeBaseInitStruct.TIM_Prescaler = 84-1; //1000 000HZ
    TIM_TimeBaseInitStruct.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStruct.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM3, &TIM_TimeBaseInitStruct);
    
    /* 配置NVIC */
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
    
    /* 开启中断，开始计时*/
    TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);
    TIM_Cmd(TIM3, ENABLE);
    
}

void TIM3_IRQHandler(void)
{
    if(TIM_GetITStatus(TIM3, TIM_IT_Update) == SET)
    {
        /* 系统时间向前推进一个单位的时间间隔 */
        lv_tick_inc(1);
		
        TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
    }
}

