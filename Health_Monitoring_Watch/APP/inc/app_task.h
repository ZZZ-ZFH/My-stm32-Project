#ifndef __APP_TASK_H
#define __APP_TASK_H

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
/* 创建启动任务并交给调度器, main 中调用一次 */
void APP_Task_Init(void);

/* 强制亮屏(任意任务上下文可调, 闹钟弹窗等场景):
 * 立即点亮背光+退出CPU睡眠, 熄屏计时自动重置 */
void APP_Task_ForceScreenOn(void);

/* 进入待机(任意任务上下文可调, 菜单待机图标场景):
 * 请求熄屏+CPU睡眠(经数个周期延时, 等待点击触摸释放),
 * 唤醒方式: 触摸/抬手手势/闹钟弹窗 */
void APP_Task_EnterStandby(void);

#endif
