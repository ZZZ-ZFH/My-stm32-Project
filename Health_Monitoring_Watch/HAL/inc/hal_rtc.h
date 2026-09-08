/**
 * @file    hal_rtc.h
 * @brief   HAL层RTC驱动: LSE时钟源/备份域/闹钟中断的通用封装
 *          与具体板级参数无关, 分频系数/中断优先级由BSP层通过配置结构体注入
 */
#ifndef __HAL_RTC_H
#define __HAL_RTC_H

#include "stm32f4xx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* RTC通用配置结构体(BSP层填充板级参数) */
typedef struct {
    uint32_t async_prediv;      /* 异步分频系数(LSE=127, 见hardware_config.h) */
    uint32_t sync_prediv;       /* 同步分频系数(LSE=255, 见hardware_config.h) */
    uint32_t alarm_irq_preempt; /* 闹钟中断抢占优先级 */
    uint32_t alarm_irq_sub;     /* 闹钟中断子优先级 */
} HAL_RTC_Config_t;

/* 通用时间/日期结构体(二进制格式, 24小时制) */
typedef struct {
    uint8_t hour;   /* 时 0-23 */
    uint8_t min;    /* 分 0-59 */
    uint8_t sec;    /* 秒 0-59 */
} HAL_RTC_Time_t;

typedef struct {
    uint8_t year;   /* 年 0-99(对应20xx) */
    uint8_t month;  /* 月 1-12 */
    uint8_t day;    /* 日 1-31 */
    uint8_t weekday;/* 星期 1-7(占位) */
} HAL_RTC_Date_t;

void     HAL_RTC_Init(const HAL_RTC_Config_t *cfg);       /* 初始化: PWR时钟/备份域解锁/LSE/分频/24小时制 */
uint32_t HAL_RTC_BackupRead(uint32_t dr);                 /* 读备份寄存器(VBAT保持) */
void     HAL_RTC_BackupWrite(uint32_t dr, uint32_t val);  /* 写备份寄存器(VBAT保持) */
void     HAL_RTC_GetTime(HAL_RTC_Time_t *t);              /* 读时间 */
void     HAL_RTC_SetTime(const HAL_RTC_Time_t *t);        /* 写时间 */
void     HAL_RTC_GetDate(HAL_RTC_Date_t *d);              /* 读日期 */
void     HAL_RTC_SetDate(const HAL_RTC_Date_t *d);        /* 写日期 */
void     HAL_RTC_SetAlarm(uint8_t hour, uint8_t min);     /* 设置每日闹钟(EXTI线17+NVIC+闹钟A, 屏蔽周/日期) */
void     HAL_RTC_AlarmEnable(uint8_t enable);             /* 闹钟开关: 0=关(清标志), 1=开 */
void     HAL_RTC_GetAlarm(HAL_RTC_Time_t *t);             /* 读闹钟A设定时间 */
uint8_t  HAL_RTC_AlarmIsEnabled(void);                    /* 读闹钟A真实使能状态(寄存器位, VBAT保持) */
uint8_t  HAL_RTC_IRQHandler(void);                        /* 闹钟中断通用处理(清RTC/EXTI标志), 1=闹钟事件 */

#ifdef __cplusplus
}
#endif

#endif /* __HAL_RTC_H */
