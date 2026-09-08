/**
 * @file    hal_delay.c
 * @brief   HAL层通用延时驱动实现: SysTick寄存器轮询(FreeRTOS下安全)
 *          平台相关实现, 换核时仅改本文件
 */
#include "hal_delay.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx.h"   /* SysTick寄存器: 平台相关实现, 换核时仅改本文件 */

void HAL_Delay_Us(uint32_t nus)
{
    uint32_t ticks;
    uint32_t told,tnow,tcnt=0;
    uint32_t reload=SysTick->LOAD;    //系统定时器的重载值
    ticks=nus*(SystemCoreClock/1000000);//需要的节拍数
    told=SysTick->VAL;            //刚进入时的计数器值

    /* 挂起调度器[可选,会导致高优先级任务无法抢占当前任务，但能够提高当前任务时间的精确性] */
    vTaskSuspendAll();

    while(1)
    {
        tnow=SysTick->VAL;

        if(tnow!=told)
        {
            /* SYSTICK是一个递减的计数器 */
            if(tnow<told)
                tcnt+=told-tnow;
            else
                tcnt+=reload-tnow+told;

            told=tnow;

            /* 时间超过/等于要延迟的时间,则退出。*/
            if(tcnt>=ticks)
                break;
        }
    }

    /* 恢复调度器[可选] */
    xTaskResumeAll();
}

void HAL_Delay_Ms(uint32_t nms)
{
	for(uint32_t i=0; i<nms; i++)
	{
		HAL_Delay_Us(1000);
	}
}

void HAL_Delay_S(uint32_t ns)
{
	for(uint32_t i=0; i<ns; i++)
    {
		vTaskDelay(1000);
	}
}
