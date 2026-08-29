#ifndef __APP_UI_H
#define __APP_UI_H

#include "stm32f4xx.h"

/* ==================== API ==================== */
/* 创建UI全部页面并加载主页(page_home):
 * 页面结构: page_home(表盘主页) / page_menu(应用菜单) / 9个应用详情页
 * 须在 lv_init/lv_port_disp_init 之后调用 */
void APP_UI_Init(void);

/* 更新健康数据(线程安全, 可在非LVGL任务中调用):
 * 内部仅写共享变量, 由LVGL任务内的定时器刷新到界面 */
void APP_UI_SetHealthData(int32_t heart_rate, int8_t hr_valid,
                          int32_t spo2, int8_t spo2_valid);

/* 更新步数(线程安全): 刷新page_home与步数详情页 */
void APP_UI_SetSteps(uint32_t steps);

/* 设置蓝牙连接状态(线程安全): 1=已连接(图标变蓝), 0=断开(图标白色) */
void APP_UI_SetBluetoothConnected(uint8_t connected);

/* 触摸活动通知/消费(线程安全): 触摸按下时置标志,
 * IMU任务消费并重置熄屏计时(触摸期间不熄屏) */
void APP_UI_NotifyTouch(void);
uint8_t APP_UI_ConsumeTouchActivity(void);

#endif
