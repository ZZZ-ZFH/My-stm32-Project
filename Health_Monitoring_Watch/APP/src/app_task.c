/**
 * @file    app_task.c
 * @brief   FreeRTOS 任务集中管理: 任务创建与任务函数
 *          app_task1 : 启动/初始化(硬件初始化+创建子任务+运行指示灯)
 *          user_task1: LVGL 界面任务
 *          max30102_task: 心率血氧采集任务(纯中断驱动, 无轮询)
 *          dx24_task: 蓝牙接收任务(USART6空闲中断唤醒, 串口打印)
 *          icm20602_task: IMU算法任务(Middleware姿态解算+计步, 桥接HAL采集)
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
#include "imu_algo.h"
#include "st7789.h"
#include "rtc.h"

/* 任务句柄(创建后仅调试器线程感知使用, 不对外暴露) */
static TaskHandle_t app_task1_handle     = NULL;
static TaskHandle_t user_task1_handle     = NULL;
static TaskHandle_t max30102_task_handle = NULL;
static TaskHandle_t dx24_task_handle     = NULL;
static TaskHandle_t icm20602_task_handle  = NULL;

static void app_task1(void* pvParameters);
static void user_task1(void* pvParameters);
static void max30102_task(void* pvParameters);
static void dx24_task(void* pvParameters);
static void icm20602_task(void* pvParameters);

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

	// RTC初始化(LSE启动需等待稳定, 首次上电装入编译时刻, 之后VBAT保持走时)
	Rtc_Init();
	{
		uint8_t h, m, s;
		Rtc_GetTime(&h, &m, &s);
		printf("RTC: %02d:%02d:%02d\r\n", h, m, s);
	}

	// 创建user_task1任务  LVGL
	xTaskCreate((TaskFunction_t )user_task1,
			  (const char*    )"task1",
			  (uint16_t       )2048,
			  (void*          )NULL,
			  (UBaseType_t   )4,
			  (TaskHandle_t*  )&user_task1_handle);

	// 创建max30102_task任务  心率血氧传感器(软件I2C PC8=SDA PC9=SCL PA15=INT)
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

	// 创建icm20602_task任务  六轴传感器(软件I2C PB0=SCL PC13=SDA)
	xTaskCreate((TaskFunction_t )icm20602_task,
			  (const char*    )"icm20602",
			  (uint16_t       )512,
			  (void*          )NULL,
			  (UBaseType_t   )1,
			  (TaskHandle_t*  )&icm20602_task_handle);

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

// MAX30102 心率血氧任务: INT(PA15)FIFO将满唤醒+100ms兜底,
// 采样缓冲在驱动内部(静态分配), 每满500样本(5秒)计算一次,
// 内置接触检测(IR直流阈值)与生理范围过滤, 无手指时valid=0
static void max30102_task(void* pvParameters)
{
	max30102_result_t result;

	/* 先注册INT事件接收任务, 再初始化(Init内部使能EXTI) */
	MAX30102_SetNotifyTask(xTaskGetCurrentTaskHandle());

	if (MAX30102_Init() == 0)
	{
		printf("MAX30102 init failed! Check PC8=SDA PC9=SCL PA15=INT\r\n");
		vTaskDelete(NULL);   // 删除自身
	}
	printf("MAX30102 init OK\r\n");

	while(1)
	{
		/* 中断唤醒为主, 100ms超时兜底: 覆盖100sps采样不溢出FIFO32,
		 * 且INT电平不稳(模块上拉1.8V)时保证数据不丢 */
		MAX30102_WaitSampleEvent(100);

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

// IMU算法任务: Middleware层姿态解算+计步+抬手检测, 50ms周期(桥接HAL采集)
// 抬手亮屏: 检测到抬手手势->背光开; 20秒无任何操作(触摸/抬手)->背光关+CPU睡眠模式
// (软件I2C PB0=SCL PC13=SDA, 初始化含陀螺零偏校准需静止约2秒)
#define WRIST_SCREEN_TIMEOUT_MS   20000u      /* 无操作熄屏时间(触摸/抬手重置) */

volatile uint8_t g_screen_off = 0;   /* 熄屏标志: 空闲钩子据此让CPU进睡眠模式 */

static void icm20602_task(void* pvParameters)
{
	imu_result_t result;
	uint8_t  screen_on   = 1;                 /* 开机默认亮屏 */
	uint32_t screen_ms   = 0;                 /* 上次亮屏/交互时刻 */

	if (IMU_Alg_Init() == 0)
	{
		printf("IMU algo init failed! Check PB0=SCL PC13=SDA\r\n");
		vTaskDelete(NULL);   // 删除自身
	}
	printf("IMU algo init OK\r\n");

	while(1)
	{
		IMU_Alg_Process(50);   // 50ms采样周期(20Hz), 与vTaskDelay一致
		IMU_Alg_GetResult(&result);

		/* 步数推送到应用层UI(线程安全: 内部仅写共享变量) */
		APP_UI_SetSteps((uint32_t)result.steps);

		/* 触摸活动 -> 亮屏并重置熄屏计时(点击/滑动期间不熄屏) */
		if (APP_UI_ConsumeTouchActivity())
		{
			if (!screen_on)
			{
				ST7789_Set_Backlight(100);
				screen_on = 1;
				g_screen_off = 0;      /* 唤醒CPU退出睡眠模式 */
				printf("Touch -> screen ON\r\n");
			}
			screen_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
		}

		/* 抬手手势 -> 亮屏(事件型, 仅一帧有效) */
		if (result.wrist_raise)
		{
			if (!screen_on)
			{
				ST7789_Set_Backlight(100);
				screen_on = 1;
				g_screen_off = 0;      /* 唤醒CPU退出睡眠模式 */
				printf("Wrist raise -> screen ON\r\n");
			}
			screen_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
		}

		/* 亮屏超时 -> 熄屏(空闲钩子检测到熄屏后让CPU进睡眠模式) */
		if (screen_on)
		{
			uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
			if (now - screen_ms > WRIST_SCREEN_TIMEOUT_MS)
			{
				ST7789_Set_Backlight(0);
				screen_on = 0;
				g_screen_off = 1;
				printf("Timeout -> screen OFF + CPU sleep\r\n");
			}
		}

//		printf("Roll=%.1f Pitch=%.1f Cadence=%.0f Steps=%d\r\n",
//		       result.roll, result.pitch, result.cadence, (int)result.steps);
		vTaskDelay(50);        // 50ms周期
	}
}

/* FreeRTOS空闲钩子: 熄屏后CPU进入睡眠模式(WFI), 实现低功耗
 * - 睡眠模式(非深度睡眠): SysTick/外设中断正常工作, 任何中断唤醒CPU继续调度
 * - 触摸轮询/IMU采集任务照常运行, 检测到触摸或抬手即亮屏并退出睡眠
 * - 亮屏期间不执行WFI, 保证界面渲染的响应速度 */
void vApplicationIdleHook(void)
{
	if (g_screen_off)
	{
		SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;   /* 睡眠模式(浅睡), 非深度睡眠 */
		__WFI();                              /* 等待中断: CPU暂停, 中断唤醒后继续 */
	}
}
