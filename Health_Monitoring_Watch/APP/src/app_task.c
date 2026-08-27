/**
 * @file    app_task.c
 * @brief   FreeRTOS 任务集中管理: 任务创建与任务函数
 *          app_task1 : 启动/初始化(硬件初始化+创建子任务+运行指示灯)
 *          user_task1: LVGL 界面任务
 *          max30102_task: 心率血氧采集任务(纯中断驱动, 无轮询)
 *          dx24_task: 蓝牙接收任务(USART6空闲中断唤醒, 串口打印)
 */
#include "app_task.h"

#include "stm32f4xx.h"
#include "stdio.h"
#include "string.h"

#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"

#include "app_ui.h"

#include "tim3.h"
#include "led.h"
#include "uart1.h"
#include "max30102.h"
#include "dx24.h"

/* 任务句柄(创建后仅调试器线程感知使用, 不对外暴露) */
static TaskHandle_t app_task1_handle     = NULL;
static TaskHandle_t user_task1_handle     = NULL;
static TaskHandle_t max30102_task_handle = NULL;
static TaskHandle_t dx24_task_handle     = NULL;

static void app_task1(void* pvParameters);
static void user_task1(void* pvParameters);
static void max30102_task(void* pvParameters);
static void dx24_task(void* pvParameters);

void APP_Task_Init(void)
{
	xTaskCreate((TaskFunction_t )app_task1,           // 任务入口函数
			  (const char*    )"app_task1",           // 任务名字
			  (uint16_t       )512,                   // 任务栈大小  字为单位
			  (void*          )NULL,                  // 任务入口函数参数
			  (UBaseType_t    )4,                     // 任务的优先级 数字越大 优先级越高
			  (TaskHandle_t*  )&app_task1_handle);    // 任务控制块指针
}

static void app_task1(void* pvParameters)
{
	//硬件初始化
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4); //中断优先级分组 4
	LED_Init();
	Uart1_Init(9600);
	printf("Uart1_Init\r\n");

	// 创建user_task1任务  LVGL
	xTaskCreate((TaskFunction_t )user_task1,
			  (const char*    )"task1",
			  (uint16_t       )2048,
			  (void*          )NULL,
			  (UBaseType_t   )4,
			  (TaskHandle_t*  )&user_task1_handle);

	// 创建max30102_task任务  心率血氧传感器(软件I2C PC8=SDA PC9=SCL PC11=INT)
	xTaskCreate((TaskFunction_t )max30102_task,
			  (const char*    )"max30102",
			  (uint16_t       )512,
			  (void*          )NULL,
			  (UBaseType_t   )3,
			  (TaskHandle_t*  )&max30102_task_handle);

	// 创建dx24_task任务  蓝牙模块(USART6 PC6-TX PC7-RX, 空闲中断唤醒)
	xTaskCreate((TaskFunction_t )dx24_task,
			  (const char*    )"dx24",
			  (uint16_t       )512,
			  (void*          )NULL,
			  (UBaseType_t   )2,
			  (TaskHandle_t*  )&dx24_task_handle);

    while(1)
    {
		// 任务状态正常运行的指示灯
        GPIO_ToggleBits(GPIOF, GPIO_Pin_9);

        vTaskDelay(1000);
    }
}

static void user_task1(void* pvParameters)
{
	// LVGL 初始化: 显示(ST7789 240x300) + 触摸(CST816) + 1ms tick
	lv_init();
	lv_port_disp_init();
	lv_port_indev_init();
	TIM3_Init();

	APP_UI_Init();         // 应用层UI: 健康监测表盘(心率+血氧)

	while(1)
	{
		lv_task_handler();   // LVGL任务处理(渲染+输入+定时器)
		vTaskDelay(5);
	}
}

// MAX30102 心率血氧任务: INT(PC7)FIFO将满唤醒, 采样缓冲在驱动
// 内部(静态分配), 每满500样本(5秒)计算一次心率/血氧并打印到串口
static void max30102_task(void* pvParameters)
{
	max30102_result_t result;

	/* 先注册INT事件接收任务, 再初始化(Init内部使能EXTI) */
	MAX30102_SetNotifyTask(xTaskGetCurrentTaskHandle());

	if (MAX30102_Init() == 0)
	{
		printf("MAX30102 init failed! Check PC8=SDA PC9=SCL PC11=INT\r\n");
		vTaskDelete(NULL);   // 删除自身
	}
	printf("MAX30102 init OK\r\n");

	while(1)
	{
		/* 纯中断驱动: 无INT事件时永久阻塞, 不轮询总线 */
		MAX30102_WaitSampleEvent(0);

		if (MAX30102_Process(&result) == 1)
		{
			printf("HR=%d, HRvalid=%d, SpO2=%d, SpO2Valid=%d\r\n",
			       (int)result.heart_rate, (int)result.hr_valid,
			       (int)result.spo2, (int)result.spo2_valid);

			/* 推送到应用层UI(线程安全: 内部仅写共享变量) */
		APP_UI_SetHealthData(result.heart_rate, result.hr_valid,
		                     result.spo2, result.spo2_valid);
	}
	}
}

// DX24蓝牙任务: USART6(PC6-TX PC7-RX)空闲中断唤醒接收数据并打印;
// STATE脚(PA0)轮询连接状态(200ms周期+消抖), 变化时刷新UI蓝牙图标颜色
static void dx24_task(void* pvParameters)
{
	uint8_t state_stable  = 0;   /* 消抖后的稳定连接状态 */
	uint8_t state_cnt     = 0;   /* 消抖计数: 连续N次读数一致才生效 */
	uint8_t state_last    = 0;   /* 上一次原始读数 */

	/* 先注册IDLE事件接收任务, 再初始化(Init内部使能中断) */
	DX24_SetNotifyTask(xTaskGetCurrentTaskHandle());
	DX24_Init(9600);
	printf("DX24 bluetooth uart init\r\n");

	while(1)
	{
		/* 等待一帧数据到达, 最多阻塞200ms后轮询STATE脚 */
		ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(200));

		if(dx_rx_flag == 1)
		{
			/* 打印本帧蓝牙数据(缓冲区自带'\0'终止符) */
			printf("BT RX(%d): %s\r\n", (int)dx_rx_len, (char*)dx_rx_buf);

			memset(dx_rx_buf, 0, sizeof(dx_rx_buf));
			dx_rx_len  = 0;
			dx_rx_flag = 0;
		}

		/* 连接状态消抖: 连续3次(约600ms)读数一致才切换 */
		{
			uint8_t state_raw = DX24_IsConnected();

			if(state_raw != state_last)
			{
				state_last = state_raw;   /* 电平变化, 重新计数 */
				state_cnt  = 0;
			}
			else if(state_cnt < 3)
			{
				state_cnt++;
				if(state_cnt == 3 && state_raw != state_stable)
				{
					/* 状态确认变化, 通知UI(白<->蓝) */
					state_stable = state_raw;
					APP_UI_SetBluetoothConnected(state_stable);
					printf("BT %s\r\n",
					       state_stable ? "connected" : "disconnected");
				}
			}
		}
	}
}
