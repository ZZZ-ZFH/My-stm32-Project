#include "stm32f4xx.h"

#include "stdio.h"
#include "string.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "event_groups.h"

#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "lv_demo_widgets.h"

#include "tim3.h"
#include "st7789.h"
#include "cst816.h"
#include "led.h"
#include "uart1.h"

uint8_t rx_buf[MAX_FRAME_LEN] = {0};
u16  rx_index = 0;
uint8_t  rx_flag = 0;


static u32 value = 0;
TimerHandle_t g_timer1;

TaskHandle_t app_task1_handle = NULL;
TaskHandle_t user_task1_handle = NULL;

/* 任务1 */ 
static void app_task1(void* pvParameters);  
static void user_task1(void* pvParameters);



// 栈溢出钩子: 任务栈耗尽时打印任务名, 便于定位卡死原因
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
	printf("!!! Stack Overflow: %s\r\n", pcTaskName);
	while(1);
}

int main()
{
	// 创建app_task1任务  启动/初始化
	xTaskCreate((TaskFunction_t )app_task1,           // 任务入口函数
			  (const char*    )"app_task1",           // 任务名字
			  (uint16_t       )512,                   // 任务栈大小  字为单位
			  (void*          )NULL,                  // 任务入口函数参数
			  (UBaseType_t    )4,                     // 任务的优先级 数字越大 优先级越高
			  (TaskHandle_t*  )&app_task1_handle);    // 任务控制块指针
				  
	// 开启任务调度
	vTaskStartScheduler(); 		
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
			  (UBaseType_t    )4,                     
			  (TaskHandle_t*  )&user_task1_handle);    

			  
#if 0	
	UBaseType_t task_priority = 0;
    UBaseType_t task_num = 0;
    UBaseType_t task_num2 = 0;
    
    TaskStatus_t task_status2[1] = 0;
    TaskHandle_t task_handle = 0;
    UBaseType_t task_stack_remain_min = 0; 
    eTaskState task_state = 0;

    /* 查询任务优先级 */
    task_priority = uxTaskPriorityGet(user_task1_handle);
    printf("task1任务优先级=%lu....\r\n", task_priority);
    task_priority = uxTaskPriorityGet(user_task2_handle);
    printf("task2任务优先级=%lu....\r\n", task_priority);

    /* 设置任务优先级 */
    vTaskPrioritySet(user_task1_handle, 3);
    task_priority = uxTaskPriorityGet(user_task1_handle);
    printf("task1任务优先级=%lu....\r\n", task_priority);

    /* 查询任务数量：包含启动调度器时底层启动的任务 */
    task_num = uxTaskGetNumberOfTasks();
    printf("任务数量=%lu....\r\n", task_num);

    /* 获取系统状态 */
	TaskStatus_t task_status[task_num];
    task_num2 = uxTaskGetSystemState(task_status, task_num, NULL);
    printf("任务名\t\t任务编号\t任务优先级\r\n");
    for (uint8_t i = 0; i < task_num2; i++)
    {
        printf("%-15s\t%ld\t%ld\r\n",
               task_status[i].pcTaskName,
               task_status[i].xTaskNumber,
               task_status[i].uxCurrentPriority);
    }

    /* 获取单个任务信息 */
    vTaskGetInfo(user_task1_handle,
                 task_status2,
                 pdTRUE,
                 eInvalid);
    printf("任务名：%s\r\n", task_status2->pcTaskName);
    printf("任务编号：%d\r\n", task_status2->xTaskNumber);
    printf("任务优先级：%d\r\n", task_status2->uxCurrentPriority);
    printf("任务状态：%d\r\n", task_status2->eCurrentState);


    /* 获取指定任务的任务栈历史最小剩余值 */
    task_stack_remain_min = uxTaskGetStackHighWaterMark( user_task2_handle ); 
    printf("task2任务栈历史最小值=%ld\r\n",task_stack_remain_min);

    /* 获取指定任务的状态 */
    task_state = eTaskGetState( user_task2_handle );
    printf("task2当前任务状态=%d\r\n",task_state);


#endif			  
    while(1)
    {
		// 任务状态正常运行的指示灯
        GPIO_ToggleBits(GPIOF, GPIO_Pin_9);   
		
        vTaskDelay(1000);       
    }
} 

// 声明一个自定义的 TTF 字体
LV_FONT_DECLARE(my_font);

static void user_task1(void* pvParameters)
{
	// LVGL 完整测试: 显示widgets demo + 触摸交互 
	lv_init();               // LVGL内核初始化 
	lv_port_disp_init();     // 显示移植: ST7789 240x300 (含屏幕初始化+背光) 
	lv_port_indev_init();    // 输入移植: CST816触摸 (含校准) 
	TIM3_Init();             // 1ms定时, 中断里lv_tick_inc(1) 
							  
	lv_demo_widgets();       // 运行官方控件demo: 三个标签页+滑条/开关/按钮 
							  
	while(1)                  
	{                         
		lv_task_handler();   // LVGL任务处理(渲染+输入+定时器) 
		vTaskDelay(5);
	}
}
\
