/**
 * @file    bsp_pwr.c
 * @brief   BSP层电源管理实现
 *          Cortex-M核心寄存器(SCB/WFI)访问集中于此, 换平台时仅改本文件
 *          (同为Cortex-M核则本文件可直接复用)
 */
#include "bsp_pwr.h"
#include "stm32f4xx.h"

void BSP_PWR_NVICGroupInit(void)
{
    /* ST标准库专有接口: 4位全用作抢占优先级(FreeRTOS要求) */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);
}

void BSP_PWR_EnterSleep(void)
{
    SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;   /* 睡眠模式(浅睡), 非深度睡眠 */
    __WFI();                              /* 等待中断: CPU暂停, 中断唤醒后继续 */
}
