/**
 * @file    bsp_rtc.h
 * @brief   BSP层板级RTC适配: LSE驱动, VBAT掉电保持走时
 */
#ifndef __BSP_RTC_H
#define __BSP_RTC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* BSP层RTC接口(供应用层调用) */
void BSP_RTC_Init(void);                                    /* 初始化RTC(首次上电装入编译时刻) */
void BSP_RTC_GetTime(uint8_t *hour, uint8_t *min, uint8_t *sec);   /* 读时间: 时/分/秒 */
void BSP_RTC_GetDate(uint8_t *year, uint8_t *month, uint8_t *day); /* 读日期: 年(0-99对应20xx)/月/日 */
void BSP_RTC_SetTime(uint8_t hour, uint8_t min, uint8_t sec);      /* 写时间(手机/本机设置) */
void BSP_RTC_SetDate(uint8_t year, uint8_t month, uint8_t day);    /* 写日期: 年(0-99对应20xx) */
void BSP_RTC_SetAlarm(uint8_t hour, uint8_t min);                  /* 设置每日闹钟(时:分, 到点蜂鸣) */
void BSP_RTC_GetAlarm(uint8_t *hour, uint8_t *min);                /* 读闹钟设定时间 */
void BSP_RTC_AlarmEnable(uint8_t enable);                          /* 闹钟开关: 0=关(停铃), 1=开 */
uint8_t BSP_RTC_AlarmIsEnabled(void);                              /* 读RTC真实使能状态(VBAT保持, 开机同步UI开关) */
uint8_t BSP_RTC_AlarmRinging(void);                                /* 闹钟正在响铃(供UI轮询弹窗) */
void BSP_RTC_AlarmStop(void);                                      /* 手动停止响铃(弹窗停止按钮) */
void BSP_RTC_AlarmTick(void);                                      /* 闹铃超时管理(需每秒调用一次, 响ALARM_BEEP_SEC后自动停) */
void BSP_RTC_SaveSteps(uint32_t steps, uint8_t year, uint8_t month, uint8_t day); /* 步数存备份寄存器(VBAT掉电保持) */
uint8_t BSP_RTC_LoadSteps(uint32_t *steps, uint8_t *year, uint8_t *month, uint8_t *day); /* 读保存的步数, 0=无有效数据 */

#ifdef __cplusplus
}
#endif

#endif /* __BSP_RTC_H */
