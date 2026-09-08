/**
 * @file    bsp_rtc.c
 * @brief   BSP层板级RTC适配实现: 经由HAL层(hal_rtc)操作RTC外设
 *          LSE 32.768kHz + 备份域, 首次上电装入编译时刻, VBAT保持走时
 *          分频系数/备份寄存器标记/中断优先级集中于 hardware_config.h
 */
#include "bsp_rtc.h"
#include "hal_rtc.h"
#include "bsp_beep.h"
#include "hardware_config.h"
#include <string.h>
#include <stdio.h>

/* RTC板级配置描述符(注入HAL层) */
static const HAL_RTC_Config_t s_rtc_cfg = {
    .async_prediv      = HW_RTC_ASYNC_PREDIV,
    .sync_prediv       = HW_RTC_SYNC_PREDIV,
    .alarm_irq_preempt = HW_RTC_ALARM_IRQ_PREEMPT,
    .alarm_irq_sub     = HW_RTC_ALARM_IRQ_SUB,
};

/* 闹钟响铃持续秒数(到时由AlarmTick自动停止) */
#define ALARM_BEEP_SEC  10

/* 剩余响铃秒数(ISR置位, AlarmTick每秒递减) */
static volatile uint8_t s_beep_sec = 0;

/* 解析__DATE__("Mmm dd yyyy")和__TIME__("hh:mm:ss") -> RTC初值(编译时刻) */
static void compile_time_to_rtc(uint8_t *h, uint8_t *m, uint8_t *s,
                                uint8_t *year, uint8_t *month, uint8_t *day)
{
    static const char mon_str[12][4] = {
        "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    char mstr[4] = {0};
    int  dd = 1, yyyy = 2026, hh = 0, mm = 0, ss = 0;
    uint8_t i;

    /* __DATE__: "Mmm dd yyyy" 或 "Mmm  d yyyy"(日小于10时前导空格) */
    sscanf(__DATE__, "%3s %d %d", mstr, &dd, &yyyy);
    sscanf(__TIME__, "%d:%d:%d", &hh, &mm, &ss);

    *month = 1;
    for (i = 0; i < 12; i++)
    {
        if (strcmp(mstr, mon_str[i]) == 0) { *month = (uint8_t)(i + 1); break; }
    }
    *day   = (uint8_t)dd;
    *year  = (uint8_t)(yyyy % 100);
    *h     = (uint8_t)hh;
    *m     = (uint8_t)mm;
    *s     = (uint8_t)ss;
}

void BSP_RTC_Init(void)
{
    /* 1.HAL层初始化: PWR时钟/备份域解锁/LSE/分频/24小时制 */
    HAL_RTC_Init(&s_rtc_cfg);

    /* 2.首次上电(备份寄存器无标记)时装入编译时刻, 之后VBAT保持走时 */
    if (HAL_RTC_BackupRead(RTC_BKP_DR0) != HW_RTC_BKP_DR)
    {
        HAL_RTC_Time_t t;
        HAL_RTC_Date_t d;

        compile_time_to_rtc(&t.hour, &t.min, &t.sec,
                            &d.year, &d.month, &d.day);
        d.weekday = 1;   /* 星期(界面未显示, 占位) */

        HAL_RTC_SetTime(&t);
        HAL_RTC_SetDate(&d);

        /* 往备份寄存器写入标记 */
        HAL_RTC_BackupWrite(RTC_BKP_DR0, HW_RTC_BKP_DR);
    }
}

/* 读取RTC时间(二进制): 时/分/秒 */
void BSP_RTC_GetTime(uint8_t *hour, uint8_t *min, uint8_t *sec)
{
    HAL_RTC_Time_t t;

    HAL_RTC_GetTime(&t);
    *hour = t.hour;
    *min  = t.min;
    *sec  = t.sec;
}

/* 读取RTC日期(二进制): 年(0-99对应20xx)/月/日 */
void BSP_RTC_GetDate(uint8_t *year, uint8_t *month, uint8_t *day)
{
    HAL_RTC_Date_t d;

    HAL_RTC_GetDate(&d);
    *year  = d.year;
    *month = d.month;
    *day   = d.day;
}

/* 写时间(手机/本机设置) */
void BSP_RTC_SetTime(uint8_t hour, uint8_t min, uint8_t sec)
{
    HAL_RTC_Time_t t = { .hour = hour, .min = min, .sec = sec };

    HAL_RTC_SetTime(&t);
}

/* 写日期: 年(0-99对应20xx) */
void BSP_RTC_SetDate(uint8_t year, uint8_t month, uint8_t day)
{
    HAL_RTC_Date_t d = { .year = year, .month = month, .day = day, .weekday = 1 };

    HAL_RTC_SetDate(&d);
}

void BSP_RTC_SetAlarm(uint8_t hour, uint8_t min)
{
    HAL_RTC_SetAlarm(hour, min);
}

/* 闹钟开关: 0=关(DISABLE闹钟A并停铃), 1=开 */
void BSP_RTC_AlarmEnable(uint8_t enable)
{
    HAL_RTC_AlarmEnable(enable);
    if (!enable)
    {
        s_beep_sec = 0;
        BSP_BEEP_Off();
    }
}

/* 读取闹钟A设定时间(供响铃弹窗显示) */
void BSP_RTC_GetAlarm(uint8_t *hour, uint8_t *min)
{
    HAL_RTC_Time_t t;

    HAL_RTC_GetAlarm(&t);
    if (hour) *hour = t.hour;
    if (min)  *min  = t.min;
}

/* 闹钟是否已使能: 读RTC控制寄存器ALRAE位(备份域, VBAT重启保持)
 * 供UI在开机时同步闹钟开关的真实状态 */
uint8_t BSP_RTC_AlarmIsEnabled(void)
{
    return HAL_RTC_AlarmIsEnabled();
}

/* 闹钟正在响铃: 供UI秒级轮询显示弹窗 */
uint8_t BSP_RTC_AlarmRinging(void)
{
    return (s_beep_sec > 0) ? 1 : 0;
}

/* 手动停止响铃(弹窗"停止"按钮) */
void BSP_RTC_AlarmStop(void)
{
    s_beep_sec = 0;
    BSP_BEEP_Off();
}

/* 闹铃超时管理: 响铃ALARM_BEEP_SEC后自动停止(每秒调用一次) */
void BSP_RTC_AlarmTick(void)
{
    if (s_beep_sec > 0)
    {
        s_beep_sec--;
        if (s_beep_sec == 0)
        {
            BSP_BEEP_Off();
        }
    }
}

/* 闹钟A的中断向量函数: 转调HAL层通用中断处理 */
void RTC_Alarm_IRQHandler(void)
{
    if (HAL_RTC_IRQHandler())
    {
        /* 业务处理: 闹钟响铃(AlarmTick到时自动停) */
        BSP_BEEP_On();
        s_beep_sec = ALARM_BEEP_SEC;
    }
}

/* ==================== 当日步数掉电保存(备份寄存器) ====================
 * BKP_DR2: 步数(32位)
 * BKP_DR3: 0xA5000000 | 年<<16 | 月<<8 | 日 (日期不符则视为陈旧数据)
 * 备份寄存器由VBAT保持, 掉电不丢; 写寄存器无Flash磨损, 步数变化即存 */

#define BKP_STEPS_MAGIC   0xA5000000u
#define BKP_DR_STEPS     RTC_BKP_DR2
#define BKP_DR_STEPS_DATE RTC_BKP_DR3

void BSP_RTC_SaveSteps(uint32_t steps, uint8_t year, uint8_t month, uint8_t day)
{
    HAL_RTC_BackupWrite(BKP_DR_STEPS, steps);
    HAL_RTC_BackupWrite(BKP_DR_STEPS_DATE,
                        BKP_STEPS_MAGIC | ((uint32_t)year << 16)
                        | ((uint32_t)month << 8) | (uint32_t)day);
}

/* 读取保存的步数: 返回1=数据有效且日期==today(由调用方比较日期),
 * 保存日期通过today参数返回, 与当前日期不符时返回0 */
uint8_t BSP_RTC_LoadSteps(uint32_t *steps,
                          uint8_t *year, uint8_t *month, uint8_t *day)
{
    uint32_t d = HAL_RTC_BackupRead(BKP_DR_STEPS_DATE);

    if ((d & 0xFF000000u) != BKP_STEPS_MAGIC)
        return 0;   /* 无有效数据(首次上电/VBAT掉过) */
    if (steps) *steps = HAL_RTC_BackupRead(BKP_DR_STEPS);
    if (year)  *year  = (uint8_t)((d >> 16) & 0xFF);
    if (month) *month = (uint8_t)((d >> 8) & 0xFF);
    if (day)   *day   = (uint8_t)(d & 0xFF);
    return 1;
}
