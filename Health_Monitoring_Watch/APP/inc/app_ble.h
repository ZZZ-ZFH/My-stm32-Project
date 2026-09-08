/**
 * @file    app_ble.h
 * @brief   应用层蓝牙文本命令处理
 *          手机经DX24蓝牙通道下发的文本命令在此解析并执行:
 *            GetHR / GetSpO2 / GetDate / GetTime
 *            SetDate2026.9.1 / SetTime16.41.00
 */
#ifndef __APP_BLE_H
#define __APP_BLE_H

#include <stdint.h>

/* 处理一条蓝牙文本命令(解析+执行+应答), 在dx24任务上下文调用
 * cmd: 以'\0'结尾的命令字符串(内部会剔除结尾\r\n, 允许就地修改) */
void APP_BLE_Process(char *cmd);

#endif
