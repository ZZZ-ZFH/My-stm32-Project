#ifndef __APP_UI_H
#define __APP_UI_H

#include "stm32f4xx.h"

/* ==================== API ==================== */
/* 创建健康监测主界面(表盘): 心率+血氧显示
 * 须在 lv_init/lv_port_disp_init 之后调用 */
void APP_UI_Init(void);

/* 更新健康数据(线程安全, 可在非LVGL任务中调用):
 * 内部仅写共享变量, 由LVGL任务内的定时器刷新到界面 */
void APP_UI_SetHealthData(int32_t heart_rate, int8_t hr_valid,
                          int32_t spo2, int8_t spo2_valid);

/* 设置蓝牙连接状态(线程安全): 1=已连接(图标变蓝), 0=断开(图标白色) */
void APP_UI_SetBluetoothConnected(uint8_t connected);

#endif
