/**
 * @file    svc_sys.c
 * @brief   Middleware层系统服务实现: 封装BSP系统资源
 */
#include "svc_sys.h"
#include "bsp_pwr.h"
#include "bsp_led.h"
#include "bsp_uart.h"
#include "bsp_beep.h"
#include "bsp_lvgl_tick.h"

void SVC_SYS_Init(void)
{
    BSP_PWR_NVICGroupInit();   /* 中断分组(须先于任何中断使能) */
    BSP_LED_Init();
    BSP_UART_Init();           /* 波特率等板级参数在BSP内部 */
    BSP_BEEP_Init();
}

void SVC_SYS_LedToggle(void)
{
    BSP_LED_Toggle(BSP_LED_1);
}

void SVC_SYS_TickInit(void (*tick_cb)(void))
{
    BSP_LVGL_Tick_Init(tick_cb);
}

void SVC_SYS_EnterSleep(void)
{
    BSP_PWR_EnterSleep();
}
