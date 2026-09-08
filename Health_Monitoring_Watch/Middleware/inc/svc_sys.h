/**
 * @file    svc_sys.h
 * @brief   Middleware层系统服务: 对APP屏蔽BSP系统资源(LED/调试串口/蜂鸣器/
 *          中断分组/CPU睡眠/系统节拍)的硬件细节
 */
#ifndef __SVC_SYS_H
#define __SVC_SYS_H

#ifdef __cplusplus
extern "C" {
#endif

/* 系统基础设施初始化: 中断分组+LED+调试串口+蜂鸣器(上电后首先调用) */
void SVC_SYS_Init(void);

/* 运行指示灯翻转(心跳任务) */
void SVC_SYS_LedToggle(void);

/* 初始化1ms系统节拍, 每节拍回调tick_cb(中断上下文, 须短小非阻塞),
 * LVGL心跳lv_tick_inc(1)由此注入, BSP不依赖LVGL */
void SVC_SYS_TickInit(void (*tick_cb)(void));

/* CPU进入浅睡眠(任何中断唤醒), 空闲钩子熄屏后调用 */
void SVC_SYS_EnterSleep(void);

#ifdef __cplusplus
}
#endif

#endif /* __SVC_SYS_H */
