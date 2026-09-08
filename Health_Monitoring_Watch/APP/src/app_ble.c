/**
 * @file    app_ble.c
 * @brief   应用层蓝牙文本命令处理实现
 *          手机 -> DX24蓝牙 -> 文本行 -> 此处分发执行:
 *            GetHR          : 查询心率    -> "HR:72"
 *            GetSpO2        : 查询血氧    -> "SpO2:98"
 *            GetDate        : 查询日期    -> "Date:2026.9.1"
 *            GetTime        : 查询时间    -> "Time:16.41.00"
 *            SetDate2026.9.1: 设置日期    -> 回显"Date:2026.9.1"
 *            SetTime16.41.00: 设置时间   -> 回显"Time:16.41.00"
 *          命令大小写不敏感, 结尾\r/\n/空格自动剔除;
 *          无效数值回 "XXX:ERR"
 *
 * @note    运行于dx24任务上下文(非ISR), 可安全调用BSP接口;
 *          UI侧无需通知: RTC时钟定时器每秒比对日期/时间自动刷新界面
 */
#include "app_ble.h"
#include "svc_rtc.h"
#include "svc_bt.h"
#include "app_ui.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ==================== 辅助 ==================== */
/* 蓝牙回消息(带帧尾换行, 手机串口助手按行显示) */
static void ble_reply(const char *s)
{
    SVC_BT_SendStr(s);
}

/* 大小写不敏感前缀比较: 返回1=cmd以pfx开头 */
static uint8_t prefix_ci(const char *cmd, const char *pfx)
{
    while (*pfx)
    {
        char c1 = *cmd++, c2 = *pfx++;

        if (c1 >= 'a' && c1 <= 'z') c1 -= 32;
        if (c2 >= 'a' && c2 <= 'z') c2 -= 32;
        if (c1 != c2)
            return 0;
    }
    return 1;
}

/* 日期合法(年0-99对应20xx, 闰年含2月29) */
static uint8_t date_valid(uint8_t year, uint8_t month, uint8_t day)
{
    static const uint8_t days[12] =
        {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    uint8_t max_day;

    if (month < 1 || month > 12 || day < 1)
        return 0;
    max_day = days[month - 1];
    if (month == 2 && ((2000u + year) % 4u) == 0u)
        max_day = 29;   /* 2000-2099均4年一闰 */
    return (day <= max_day) ? 1 : 0;
}

/* ==================== 命令处理 ==================== */
void APP_BLE_Process(char *cmd)
{
    char buf[40];
    int32_t hr, spo2;
    int8_t  hr_valid, spo2_valid;
    uint8_t y, mo, d, h, mi, s;

    if (cmd == NULL)
        return;

    /* 剔除结尾 \r \n 空格(串口助手常见CRLF结尾) */
    {
        uint16_t n = (uint16_t)strlen(cmd);

        while (n > 0 && (cmd[n - 1] == '\r' || cmd[n - 1] == '\n'
                         || cmd[n - 1] == ' '))
            cmd[--n] = '\0';
        if (n == 0)
            return;
    }

    if (prefix_ci(cmd, "GetHR"))
    {
        APP_UI_GetHealthData(&hr, &hr_valid, &spo2, &spo2_valid);
        if (hr_valid == 1 && hr > 0)
            snprintf(buf, sizeof(buf), "HR:%d\r\n", (int)hr);
        else
            snprintf(buf, sizeof(buf), "HR:--\r\n");
        ble_reply(buf);
    }
    else if (prefix_ci(cmd, "GetSpO2"))
    {
        APP_UI_GetHealthData(&hr, &hr_valid, &spo2, &spo2_valid);
        if (spo2_valid == 1 && spo2 > 0)
            snprintf(buf, sizeof(buf), "SpO2:%d\r\n", (int)spo2);
        else
            snprintf(buf, sizeof(buf), "SpO2:--\r\n");
        ble_reply(buf);
    }
    else if (prefix_ci(cmd, "GetDate"))
    {
        SVC_RTC_GetDate(&y, &mo, &d);
        snprintf(buf, sizeof(buf), "Date:20%02d.%d.%d\r\n",
                 (int)y, (int)mo, (int)d);
        ble_reply(buf);
    }
    else if (prefix_ci(cmd, "GetTime"))
    {
        SVC_RTC_GetTime(&h, &mi, &s);
        snprintf(buf, sizeof(buf), "Time:%02d.%02d.%02d\r\n",
                 (int)h, (int)mi, (int)s);
        ble_reply(buf);
    }
    else if (prefix_ci(cmd, "SetDate"))
    {
        int y4, mm, dd;

        /* 格式 SetDate2026.9.1 (4位年, 也兼容2位年) */
        if (sscanf(cmd + 7, "%d.%d.%d", &y4, &mm, &dd) == 3)
        {
            uint8_t year = (y4 >= 2000) ? (uint8_t)(y4 - 2000)
                                        : (uint8_t)(y4 % 100);

            if (date_valid(year, (uint8_t)mm, (uint8_t)dd))
            {
                SVC_RTC_SetDate(year, (uint8_t)mm, (uint8_t)dd);
                printf("BLE set date: 20%02d-%02d-%02d\r\n",
                       (int)year, mm, dd);
                snprintf(buf, sizeof(buf), "Date:20%02d.%d.%d\r\n",
                         (int)year, mm, dd);
            }
            else
            {
                snprintf(buf, sizeof(buf), "Date:ERR\r\n");
            }
        }
        else
        {
            snprintf(buf, sizeof(buf), "Date:ERR\r\n");
        }
        ble_reply(buf);
    }
    else if (prefix_ci(cmd, "SetTime"))
    {
        int hh, mm, ss;

        /* 格式 SetTime16.41.00 (时.分.秒) */
        if (sscanf(cmd + 7, "%d.%d.%d", &hh, &mm, &ss) == 3
            && hh >= 0 && hh < 24 && mm >= 0 && mm < 60
            && ss >= 0 && ss < 60)
        {
            SVC_RTC_SetTime((uint8_t)hh, (uint8_t)mm, (uint8_t)ss);
            printf("BLE set time: %02d:%02d:%02d\r\n", hh, mm, ss);
            snprintf(buf, sizeof(buf), "Time:%02d.%02d.%02d\r\n",
                     hh, mm, ss);
        }
        else
        {
            snprintf(buf, sizeof(buf), "Time:ERR\r\n");
        }
        ble_reply(buf);
    }
    else
    {
        /* 未识别命令: 忽略(不应答, 避免污染手机端) */
    }
}
