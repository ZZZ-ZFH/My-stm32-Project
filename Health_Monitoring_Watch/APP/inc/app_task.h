#ifndef __APP_TASK_H
#define __APP_TASK_H

#include "FreeRTOS.h"
#include "task.h"

/* 创建启动任务并交给调度器, main 中调用一次 */
void APP_Task_Init(void);

#endif
