/**
 * @file    bsp_beep.h
 * @brief   BSP层板级蜂鸣器适配: PF8, 高电平鸣响(基于HAL_GPIO通用操作)
 */
#ifndef __BSP_BEEP_H
#define __BSP_BEEP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* BSP层蜂鸣器接口(供应用层/RTC闹钟调用) */
void BSP_BEEP_Init(void);        /* 初始化蜂鸣器(默认静音) */
void BSP_BEEP_On(void);          /* 鸣响 */
void BSP_BEEP_Off(void);         /* 静音 */
void BSP_BEEP_Toggle(void);      /* 翻转状态 */

#ifdef __cplusplus
}
#endif

#endif /* __BSP_BEEP_H */
