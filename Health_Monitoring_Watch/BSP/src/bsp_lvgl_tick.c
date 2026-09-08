/**
 * @file    bsp_lvgl_tick.c
 * @brief   BSP层板级系统节拍适配实现
 *          TIM3定时参数等板级配置集中于 hardware_config.h,
 *          定时逻辑复用HAL_TIM通用实现, 心跳回调由上层注入(BSP不依赖LVGL)
 */
#include "bsp_lvgl_tick.h"
#include "hal_tim.h"
#include "hardware_config.h"

/* 上层注入的心跳回调(中断上下文, 须短小非阻塞) */
static BSP_TICK_Callback_t s_tick_cb = 0;

/* TIM更新中断回调: 转调上层注入的心跳回调 */
static void tick_update_cb(void)
{
    if (s_tick_cb != 0)
    {
        s_tick_cb();
    }
}

/* TIM3板级配置描述符: 1ms更新中断(参数见 hardware_config.h) */
static const HAL_TIM_Config_t s_tim3_cfg = {
    .instance    = HW_TICK_TIM_INSTANCE,
    .clk         = HW_TICK_TIM_CLK,
    .prescaler   = HW_TICK_TIM_PSC,
    .period      = HW_TICK_TIM_ARR,
    .irq_preempt = HW_TICK_TIM_IRQ_PREEMPT,
    .irq_sub     = HW_TICK_TIM_IRQ_SUB,
    .update_cb   = tick_update_cb,
};

void BSP_LVGL_Tick_Init(BSP_TICK_Callback_t tick_cb)
{
    s_tick_cb = tick_cb;
    HAL_TIM_BaseInit(&s_tim3_cfg);
}

/* TIM3中断向量函数: 转调HAL层通用中断处理 */
void TIM3_IRQHandler(void)
{
    HAL_TIM_IRQHandler(HW_TICK_TIM_INSTANCE);
}
