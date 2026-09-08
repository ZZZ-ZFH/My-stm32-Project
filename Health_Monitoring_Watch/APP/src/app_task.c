/**
 * @file    app_task.c
 * @brief   FreeRTOS 任务集中管理: 任务创建与任务函数
 *          硬件访问统一经Middleware服务层(svc_*), APP不直接触碰BSP/HAL
 *          app_task1 : 启动/初始化(系统服务+创建子任务+运行指示灯)
 *          user_task1: LVGL 界面任务
 *          max30102_task: 心率血氧采集任务(svc_health服务)
 *          dx24_task: 蓝牙接收任务(svc_bt服务, USART6空闲中断唤醒)
 *          icm20602_task: IMU算法任务(Middleware姿态解算+计步)
 */
#include "app_task.h"

#include "stdio.h"

#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"

#include "app_ui.h"
#include "app_ble.h"
#include "imu_algo.h"

#include "svc_sys.h"         /* 系统服务: LED/串口/蜂鸣器/睡眠/节拍 */
#include "svc_rtc.h"         /* RTC服务: 时间/日期/闹钟/步数保存 */
#include "svc_health.h"      /* 健康服务: 心率/血氧 */
#include "svc_bt.h"          /* 蓝牙服务: 收发/连接状态 */
#include "svc_display.h"     /* 显示服务: 背光亮度 */

/* 任务句柄(创建后仅调试器线程感知使用, 不对外暴露) */
static TaskHandle_t app_task_handle     = NULL;
static TaskHandle_t user_task_handle     = NULL;
static TaskHandle_t max30102_task_handle = NULL;
static TaskHandle_t dx24_task_handle     = NULL;
static TaskHandle_t icm20602_task_handle  = NULL;

static void app_task(void* pvParameters);
static void user_task(void* pvParameters);
static void max30102_task(void* pvParameters);
static void dx24_task(void* pvParameters);
static void icm20602_task(void* pvParameters);

static QueueHandle_t mutex_semphore_handle;
void APP_Task_Init(void)
{
	xTaskCreate((TaskFunction_t )app_task,           // 任务入口函数
			  (const char*    )"app_task",           // 任务名字
			  (uint16_t       )512,                   // 任务栈大小  字为单位
			  (void*          )NULL,                  // 任务入口函数参数
			  (UBaseType_t    )4,                     // 任务的优先级 数字越大 优先级越高
			  (TaskHandle_t*  )&app_task_handle);    // 任务控制块指针
}

static void app_task(void* pvParameters)
{
	//硬件初始化(系统服务: 中断分组+LED+调试串口+蜂鸣器)
	SVC_SYS_Init();
	printf("SVC_SYS_Init\r\n");
	/* 创建互斥信号量，并且主动释放一次信号量 */
    mutex_semphore_handle = xSemaphoreCreateMutex();
	 if (mutex_semphore_handle != NULL)
    {
        printf("互斥信号量创建成功\r\n");
    }
	// RTC初始化(LSE启动需等待稳定, 首次上电装入编译时刻, 之后VBAT保持走时)
	SVC_RTC_Init();
	{
		uint8_t h, m, s;
		SVC_RTC_GetTime(&h, &m, &s);
		printf("RTC: %02d:%02d:%02d\r\n", h, m, s);
	}

	// 创建user_task1任务  LVGL
	xTaskCreate((TaskFunction_t )user_task,
			  (const char*    )"user_task",
			  (uint16_t       )2048,
			  (void*          )NULL,
			  (UBaseType_t   )4,
			  (TaskHandle_t*  )&user_task_handle);

	// 创建max30102_task任务  心率血氧传感器(软件I2C PC8=SDA PC9=SCL PA15=INT)
	xTaskCreate((TaskFunction_t )max30102_task,
			  (const char*    )"max30102",
			  (uint16_t       )512,
			  (void*          )NULL,
			  (UBaseType_t   )4,
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
        SVC_SYS_LedToggle();

        vTaskDelay(1000);
    }
}

/* LVGL心跳回调: 系统节拍每1ms调用(中断上下文, 注入svc_sys) */
static void lvgl_tick_cb(void)
{
	lv_tick_inc(1);
}

static void user_task(void* pvParameters)
{
	// LVGL 初始化: 显示(ST7789 240x300) + 触摸(CST816) + 1ms tick
	lv_init();
	lv_port_disp_init();
	lv_port_indev_init();
	SVC_SYS_TickInit(lvgl_tick_cb);   /* 节拍回调注入, 驱动LVGL心跳 */

	APP_UI_Init();         // 应用层UI: 健康监测表盘(心率+血氧)

	while(1)
	{
		lv_task_handler();   // LVGL任务处理(渲染+输入+定时器)
		vTaskDelay(5);
	}
}

// MAX30102 心率血氧任务: 100ms周期轮询FIFO(无中断),
// 采样缓冲在驱动内部(静态分配), 每满500样本(5秒)计算一次,
// 内置接触检测(IR直流阈值)与生理范围过滤, 无手指时valid=0;
// 初始化失败不删任务: 空轮检测自动重连(2s周期)
static void max30102_task(void* pvParameters)
{
	svc_health_result_t result;

	if (SVC_HEALTH_Init() == 0)
	{
		/* 上电时模块可能未就绪(拔电重插场景): 不删除任务,
		 * 采样循环检测到空轮后自动重连(2s周期) */
		printf("MAX30102 init failed! Check PC8=SDA PC9=SCL, auto retry\r\n");
	}
	else
	{
		printf("MAX30102 init OK\r\n");
	}

	while(1)
	{
		/* 100ms轮询: 100sps采样下每轮约10个新样本,
		 * FIFO深32(320ms)不溢出 */
		vTaskDelay(pdMS_TO_TICKS(100));
		xSemaphoreTake(mutex_semphore_handle,portMAX_DELAY);
		if (SVC_HEALTH_Process(&result) == 1)
		{
			printf("HR=%d, HRvalid=%d, SpO2=%d, SpO2Valid=%d\r\n",
			       (int)result.heart_rate, (int)result.hr_valid,
			       (int)result.spo2, (int)result.spo2_valid);

			/* 推送到应用层UI(线程安全: 内部仅写共享变量) */
			APP_UI_SetHealthData(result.heart_rate, result.hr_valid,
								 result.spo2, result.spo2_valid);
		}
		xSemaphoreGive(mutex_semphore_handle); 
	}
}

// DX24蓝牙任务: 蓝牙服务帧到达唤醒接收处理;
// STATE脚轮询连接状态(200ms周期+消抖), 变化时刷新UI蓝牙图标颜色
static void dx24_task(void* pvParameters)
{
	uint8_t state_stable  = 0;   /* 消抖后的稳定连接状态 */
	uint8_t state_cnt     = 0;   /* 消抖计数: 连续N次读数一致才生效 */
	uint8_t state_last    = 0;   /* 上一次原始读数 */

	/* 先注册IDLE事件接收任务, 再初始化(Init内部使能中断) */
	SVC_BT_SetNotifyTask(xTaskGetCurrentTaskHandle());
	SVC_BT_Init();
	printf("DX24 bluetooth uart init\r\n");

	while(1)
	{
		/* 等待一帧数据到达, 最多阻塞200ms后轮询STATE脚 */
		ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(200));

		{
			/* 取一帧到本地再处理: 处理期间新帧到达只覆盖共享缓冲,
			 * 不影响本帧; 128字节在dx24任务栈(2KB)内可承受 */
			uint8_t  frame_buf[SVC_BT_RX_BUF_LEN];
			uint16_t frame_len = 0;

			if (SVC_BT_GetFrame(frame_buf, &frame_len))
			{
				frame_buf[frame_len] = '\0';   /* 兼容字符串打印 */
				printf("BT RX(%d): %s\r\n", (int)frame_len, (char*)frame_buf);

				/* 文本命令分发: 手机设置时间/日期, 查询心率血氧/日期/时间 */
				APP_BLE_Process((char *)frame_buf);
			}
		}

		/* 连接状态消抖: 连续3次(约600ms)读数一致才切换 */
		{
			uint8_t state_raw = SVC_BT_IsConnected();

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
volatile uint8_t g_force_screen_on = 0; /* 强制亮屏请求(闹钟弹窗等置位, 本任务消费) */

/* 强制亮屏(任意任务上下文可调): 立即点亮背光并退出CPU睡眠,
 * 熄屏计时由icm20602_task同步重置(弹窗显示期间不会超时熄屏) */
void APP_Task_ForceScreenOn(void)
{
	SVC_DISPLAY_SetBacklight(100);
	g_screen_off      = 0;
	g_force_screen_on = 1;
}

static void icm20602_task(void* pvParameters)
{
	imu_result_t result;
	uint8_t  screen_on   = 1;                 /* 开机默认亮屏 */
	uint32_t screen_ms   = 0;                 /* 上次亮屏/交互时刻 */
	uint32_t steps_last  = 0;                 /* 上次保存的步数 */
	uint8_t  sday_y = 0, sday_m = 0, sday_d = 0; /* 步数所属日期 */

	if (IMU_Alg_Init() == 0)
	{
		printf("IMU algo init failed! Check PB0=SCL PC13=SDA\r\n");
		vTaskDelete(NULL);   // 删除自身
	}
	printf("IMU algo init OK\r\n");

	/* 当日步数掉电恢复: 备份寄存器日期==今天则灌回, 否则从0开始 */
	{
		uint8_t y, m, d;
		uint32_t steps;

		if (SVC_RTC_LoadSteps(&steps, &y, &m, &d))
		{
			SVC_RTC_GetDate(&sday_y, &sday_m, &sday_d);
			if (y == sday_y && m == sday_m && d == sday_d)
			{
				IMU_Alg_SetSteps(steps);
				steps_last = steps;
				printf("Steps restored: %u\r\n", (unsigned)steps);
			}
		}
	}

	while(1)
	{
		IMU_Alg_Process(50);   // 50ms采样周期(20Hz), 与vTaskDelay一致
		IMU_Alg_GetResult(&result);

		/* 步数推送到应用层UI(线程安全: 内部仅写共享变量) */
		APP_UI_SetSteps((uint32_t)result.steps);

		/* 步数掉电保存: 变化即存(备份寄存器写无磨损);
		 * 跨天(或用户改日期)时从0重新计并更新保存日期 */
		if (result.steps != steps_last)
		{
			uint8_t y, m, d;

			SVC_RTC_GetDate(&y, &m, &d);
			if (y != sday_y || m != sday_m || d != sday_d)
			{
				sday_y = y; sday_m = m; sday_d = d;
				if (steps_last != 0)   /* 非启动首次保存: 跨天, 步数重计 */
				{
					IMU_Alg_SetSteps(0);
					result.steps = 0;
					APP_UI_SetSteps(0);
					printf("New day, steps reset\r\n");
				}
			}
			steps_last = result.steps;
			SVC_RTC_SaveSteps(steps_last, sday_y, sday_m, sday_d);
		}

		/* 强制亮屏请求(闹钟弹窗): 同步熄屏计时, 防弹窗期间超时熄屏 */
		if (g_force_screen_on)
		{
			g_force_screen_on = 0;
			if (!screen_on)
			{
				screen_on = 1;
				printf("Force -> screen ON\r\n");
			}
			screen_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
		}

		/* 触摸活动 -> 亮屏并重置熄屏计时(点击/滑动期间不熄屏) */
		if (APP_UI_ConsumeTouchActivity())
		{
			if (!screen_on)
			{
				SVC_DISPLAY_SetBacklight(100);
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
				SVC_DISPLAY_SetBacklight(100);
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
				SVC_DISPLAY_SetBacklight(0);
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
		SVC_SYS_EnterSleep();   /* CPU睡眠(平台相关指令封装于服务层) */
	}
}
