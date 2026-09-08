/**
 * @file    svc_rtc.h
 * @brief   Middleware层RTC服务: 时间/日期/闹钟/步数掉电保存,
 *          对APP屏蔽BSP_RTC的硬件细节
 */
#ifndef __SVC_RTC_H
#define __SVC_RTC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void SVC_RTC_Init(void);                                    /* 初始化RTC(首次上电装入编译时刻) */
void SVC_RTC_GetTime(uint8_t *hour, uint8_t *min, uint8_t *sec);   /* 读时间: 时/分/秒 */
void SVC_RTC_GetDate(uint8_t *year, uint8_t *month, uint8_t *day); /* 读日期: 年(0-99对应20xx)/月/日 */
void SVC_RTC_SetTime(uint8_t hour, uint8_t min, uint8_t sec);      /* 写时间(手机/本机设置) */
void SVC_RTC_SetDate(uint8_t year, uint8_t month, uint8_t day);    /* 写日期: 年(0-99对应20xx) */
void SVC_RTC_SetAlarm(uint8_t hour, uint8_t min);                  /* 设置每日闹钟(时:分, 到点蜂鸣) */
void SVC_RTC_GetAlarm(uint8_t *hour, uint8_t *min);                /* 读闹钟设定时间 */
void SVC_RTC_AlarmEnable(uint8_t enable);                          /* 闹钟开关: 0=关(停铃), 1=开 */
uint8_t SVC_RTC_AlarmIsEnabled(void);                              /* 读RTC真实使能状态(VBAT保持) */
uint8_t SVC_RTC_AlarmRinging(void);                                /* 闹钟正在响铃(供UI轮询弹窗) */
void SVC_RTC_AlarmStop(void);                                      /* 手动停止响铃(弹窗停止按钮) */
void SVC_RTC_AlarmTick(void);                                      /* 闹铃超时管理(每秒调用, 响铃超时自动停) */
void SVC_RTC_SaveSteps(uint32_t steps, uint8_t year, uint8_t month, uint8_t day); /* 步数掉电保存 */
uint8_t SVC_RTC_LoadSteps(uint32_t *steps, uint8_t *year, uint8_t *month, uint8_t *day); /* 0=无有效数据 */

#ifdef __cplusplus
}
#endif

#endif /* __SVC_RTC_H */
