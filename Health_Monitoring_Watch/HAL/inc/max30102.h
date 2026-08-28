#ifndef __MAX30102_H
#define __MAX30102_H

#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "task.h"

/* ==================== 模拟 I2C 引脚 ==================== */
/* PC8/PC9 的复用功能为 TIM3_CH3/CH4, 本驱动仅作普通GPIO开漏使用
 * (TIM3 已用作 LVGL tick, 不占用GPIO引脚, 无冲突) */
#define MAX30102_SDA_PORT    GPIOC
#define MAX30102_SDA_PIN     GPIO_Pin_8      /* PC8 数据线 */
#define MAX30102_SCL_PORT    GPIOC
#define MAX30102_SCL_PIN     GPIO_Pin_9      /* PC9 时钟线 */

/* ==================== 中断引脚 ==================== */
/* INT为开漏输出低有效, 配EXTI15下降沿, FIFO将满(A_FULL)时通知任务
 * (PC7已让给蓝牙USART6_RX; PA15为JTAG脚, 配置为输入即自动释放JTDI,
 * SWD调试口PA13/PA14不受影响) */
#define MAX30102_INT_PORT    GPIOA
#define MAX30102_INT_PIN     GPIO_Pin_15    /* PA15 中断线 */
#define MAX30102_INT_EXTI_LINE      EXTI_Line15
#define MAX30102_INT_EXTI_PORT_SRC  EXTI_PortSourceGPIOA
#define MAX30102_INT_EXTI_PIN_SRC   EXTI_PinSource15
#define MAX30102_INT_IRQn           EXTI15_10_IRQn

/* ==================== 器件参数 ==================== */
#define MAX30102_I2C_ADDR    0x57            /* 7位I2C地址 */
#define MAX30102_FIFO_DEPTH  32              /* FIFO深度(样本数) */

/* ==================== 寄存器地址 ==================== */
#define REG_INTR_STATUS_1    0x00            /* 中断状态1 */
#define REG_INTR_STATUS_2    0x01            /* 中断状态2 */
#define REG_INTR_ENABLE_1    0x02            /* 中断使能1 */
#define REG_INTR_ENABLE_2    0x03            /* 中断使能2 */
#define REG_FIFO_WR_PTR      0x04            /* FIFO写指针[4:0] */
#define REG_OVF_COUNTER      0x05            /* FIFO溢出计数[4:0] */
#define REG_FIFO_RD_PTR      0x06            /* FIFO读指针[4:0] */
#define REG_FIFO_DATA        0x07            /* FIFO数据 */
#define REG_FIFO_CONFIG      0x08            /* FIFO配置 */
#define REG_MODE_CONFIG      0x09            /* 模式配置 */
#define REG_SPO2_CONFIG      0x0A            /* SpO2配置 */
#define REG_LED1_PA          0x0C            /* 红光LED电流 */
#define REG_LED2_PA          0x0D            /* 红外LED电流 */
#define REG_PILOT_PA         0x10            /* Proximity电流 */
#define REG_MULTI_LED_CTRL1  0x11            /* 多LED控制1 */
#define REG_MULTI_LED_CTRL2  0x12            /* 多LED控制2 */
#define REG_TEMP_INTR        0x1F            /* 温度整数 */
#define REG_TEMP_FRAC        0x20            /* 温度小数 */
#define REG_TEMP_CONFIG      0x21            /* 温度配置 */
#define REG_PROX_INT_THRESH  0x30            /* 接近中断阈值 */
#define REG_REV_ID           0xFE            /* 版本ID */
#define REG_PART_ID          0xFF            /* 器件ID(固定0x15) */

/* ==================== 测量结果 ==================== */
typedef struct
{
    int32_t heart_rate;                      /* 心率 bpm */
    int8_t  hr_valid;                        /* 1=心率有效 */
    int32_t spo2;                            /* 血氧 % */
    int8_t  spo2_valid;                      /* 1=血氧有效 */
} max30102_result_t;

/* ==================== API ==================== */
uint8_t MAX30102_Init(void);                 /* 返回1=初始化成功 */
/* 注册接收INT事件的任务(须在MAX30102_Init之前调用) */
void MAX30102_SetNotifyTask(TaskHandle_t task);
/* 阻塞等待INT事件: timeout_ms=0永久阻塞(纯中断驱动, 不轮询),
 * >0为超时毫秒数, 返回收到的事件数(0=超时) */
uint32_t MAX30102_WaitSampleEvent(uint32_t timeout_ms);
/* 读取FIFO中全部待读样本并累积, 每满500样本(5秒)计算一次,
 * 返回1=result已更新 */
uint8_t MAX30102_Process(max30102_result_t *result);

#endif
