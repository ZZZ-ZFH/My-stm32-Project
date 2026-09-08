/**
 * @file    hal_rtc.c
 * @brief   HAL层RTC驱动实现: 封装ST标准库RTC/EXTI/NVIC/PWR/RCC操作
 *          换芯片时仅改本文件(接口不变)
 */
#include "hal_rtc.h"

/* Init时装入的板级配置(SetAlarm配置NVIC时使用) */
static HAL_RTC_Config_t s_cfg;

/* 初始化: PWR时钟/备份域解锁/LSE时钟源/分频与小时制 */
void HAL_RTC_Init(const HAL_RTC_Config_t *cfg)
{
    RTC_InitTypeDef RTC_InitStruct;

    s_cfg = *cfg;

    /* 1.PWR电源控制器的时钟使能 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);

    /* 2.复位后备份域(RTC寄存器/备份数据寄存器/备份SRAM)默认写保护, 先解锁 */
    PWR_BackupAccessCmd(ENABLE);

    /* 3.RTC时钟使能, 选择LSE */
    RCC_RTCCLKConfig(RCC_RTCCLKSource_LSE);
    RCC_RTCCLKCmd(ENABLE);
    RCC_LSEConfig(RCC_LSE_ON);

    /* 一个是开启时钟源需要等待稳定, 一个是切换时钟需要等待稳定 */
    while (RCC_GetFlagStatus(RCC_FLAG_LSERDY) == RESET);
    RTC_WaitForSynchro();

    /* 4.初始化RTC(同步/异步分频系数和时钟格式) */
    RTC_InitStruct.RTC_AsynchPrediv = cfg->async_prediv;
    RTC_InitStruct.RTC_SynchPrediv  = cfg->sync_prediv;
    RTC_InitStruct.RTC_HourFormat   = RTC_HourFormat_24;  /* 24小时制 */
    RTC_Init(&RTC_InitStruct);
}

uint32_t HAL_RTC_BackupRead(uint32_t dr)
{
    return RTC_ReadBackupRegister(dr);
}

void HAL_RTC_BackupWrite(uint32_t dr, uint32_t val)
{
    RTC_WriteBackupRegister(dr, val);
}

void HAL_RTC_GetTime(HAL_RTC_Time_t *t)
{
    RTC_TimeTypeDef tm;

    RTC_GetTime(RTC_Format_BIN, &tm);
    t->hour = (uint8_t)tm.RTC_Hours;
    t->min  = (uint8_t)tm.RTC_Minutes;
    t->sec  = (uint8_t)tm.RTC_Seconds;
}

void HAL_RTC_SetTime(const HAL_RTC_Time_t *t)
{
    RTC_TimeTypeDef tm;

    tm.RTC_H12      = RTC_H12_AM;   /* 24小时制下此参数无效 */
    tm.RTC_Hours    = t->hour;
    tm.RTC_Minutes  = t->min;
    tm.RTC_Seconds  = t->sec;
    RTC_SetTime(RTC_Format_BIN, &tm);
}

void HAL_RTC_GetDate(HAL_RTC_Date_t *d)
{
    RTC_DateTypeDef dt;

    RTC_GetDate(RTC_Format_BIN, &dt);
    d->year    = (uint8_t)dt.RTC_Year;
    d->month   = (uint8_t)dt.RTC_Month;
    d->day     = (uint8_t)dt.RTC_Date;
    d->weekday = (uint8_t)dt.RTC_WeekDay;
}

void HAL_RTC_SetDate(const HAL_RTC_Date_t *d)
{
    RTC_DateTypeDef dt;

    dt.RTC_Year    = d->year;
    dt.RTC_Month   = d->month;
    dt.RTC_Date    = d->day;
    dt.RTC_WeekDay = d->weekday;
    RTC_SetDate(RTC_Format_BIN, &dt);
}

void HAL_RTC_SetAlarm(uint8_t hour, uint8_t min)
{
    RTC_AlarmTypeDef alarm;
    EXTI_InitTypeDef EXTI_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    /* 1.EXTI线17映射RTC闹钟事件(重复配置无害) */
    EXTI_InitStructure.EXTI_Line    = EXTI_Line17;
    EXTI_InitStructure.EXTI_Mode   = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    /* 2.NVIC中断优先级(参数来自Init时装入的板级配置) */
    NVIC_InitStructure.NVIC_IRQChannel = RTC_Alarm_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = s_cfg.alarm_irq_preempt;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = s_cfg.alarm_irq_sub;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* 3.配置闹钟A时间: 屏蔽周和日期, 每天生效 */
    RTC_AlarmCmd(RTC_Alarm_A, DISABLE);
    RTC_AlarmStructInit(&alarm);
    alarm.RTC_AlarmTime.RTC_H12     = RTC_H12_AM;
    alarm.RTC_AlarmTime.RTC_Hours   = hour;
    alarm.RTC_AlarmTime.RTC_Minutes = min;
    alarm.RTC_AlarmTime.RTC_Seconds = 0;
    alarm.RTC_AlarmMask = RTC_AlarmMask_DateWeekDay;
    RTC_SetAlarm(RTC_Format_BIN, RTC_Alarm_A, &alarm);

    /* 4.允许闹钟A中断并清空历史标志(防改时间后立刻误触发) */
    RTC_ITConfig(RTC_IT_ALRA, ENABLE);
    RTC_ClearFlag(RTC_FLAG_ALRAF);
    EXTI_ClearITPendingBit(EXTI_Line17);
    RTC_AlarmCmd(RTC_Alarm_A, ENABLE);
}

void HAL_RTC_AlarmEnable(uint8_t enable)
{
    if (enable)
    {
        RTC_AlarmCmd(RTC_Alarm_A, ENABLE);
    }
    else
    {
        RTC_AlarmCmd(RTC_Alarm_A, DISABLE);
        RTC_ClearFlag(RTC_FLAG_ALRAF);
        EXTI_ClearITPendingBit(EXTI_Line17);
    }
}

void HAL_RTC_GetAlarm(HAL_RTC_Time_t *t)
{
    RTC_AlarmTypeDef alarm;

    RTC_GetAlarm(RTC_Format_BIN, RTC_Alarm_A, &alarm);
    t->hour = (uint8_t)alarm.RTC_AlarmTime.RTC_Hours;
    t->min  = (uint8_t)alarm.RTC_AlarmTime.RTC_Minutes;
}

uint8_t HAL_RTC_AlarmIsEnabled(void)
{
    return (RTC->CR & RTC_CR_ALRAE) ? 1 : 0;
}

uint8_t HAL_RTC_IRQHandler(void)
{
    if (RTC_GetITStatus(RTC_IT_ALRA) != RESET)
    {
        RTC_ClearITPendingBit(RTC_IT_ALRA);   /* 清RTC标志 */
        EXTI_ClearITPendingBit(EXTI_Line17);  /* 清EXTI标志 */
        return 1;   /* 闹钟事件 */
    }
    return 0;
}
