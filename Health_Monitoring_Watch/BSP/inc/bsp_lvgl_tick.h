/**
 * @file    bsp_lvgl_tick.h
 * @brief   BSP层板级系统节拍适配: TIM3产生1ms节拍, 心跳回调由上层注入
 *          (BSP不依赖LVGL, 回调在中断上下文执行, 须短小非阻塞)
 */
#ifndef __BSP_LVGL_TICK_H
#define __BSP_LVGL_TICK_H

#ifdef __cplusplus
extern "C" {
#endif

/* 1ms节拍回调类型(中断上下文) */
typedef void (*BSP_TICK_Callback_t)(void);

/* 初始化系统节拍(TIM3, 1ms周期), 每个节拍调用tick_cb */
void BSP_LVGL_Tick_Init(BSP_TICK_Callback_t tick_cb);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_LVGL_TICK_H */
