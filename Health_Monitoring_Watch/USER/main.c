#include "stdio.h"

#include "FreeRTOS.h"
#include "task.h"

#include "app_task.h"

// 栈溢出钩子: 任务栈耗尽时打印任务名, 便于定位卡死原因
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
	printf("!!! Stack Overflow: %s\r\n", pcTaskName);
	while(1);
}

int main()
{
	// 创建任务(集中在 app_task.c)
	APP_Task_Init();

	// 开启任务调度
	vTaskStartScheduler();
}
