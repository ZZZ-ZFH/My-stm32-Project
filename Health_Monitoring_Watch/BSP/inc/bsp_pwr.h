/**
 * @file    bsp_pwr.h
 * @brief   BSP层电源管理: CPU睡眠/唤醒(架构相关的SCB/WFI封装于此,
 *          应用层不直接触碰内核寄存器)
 */
#ifndef __BSP_PWR_H
#define __BSP_PWR_H

#include <stdint.h>

/* NVIC中断优先级分组(须在任何中断使能前调用一次)
 * 分组策略属于平台/芯片相关配置, 集中于BSP */
void BSP_PWR_NVICGroupInit(void);

/* CPU进入睡眠(浅睡, 任何中断唤醒后继续调度); 非深度睡眠 */
void BSP_PWR_EnterSleep(void);

#endif /* __BSP_PWR_H */
