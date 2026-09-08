/**
 * @file    hardware_config.h
 * @brief   板级硬件配置中心: 全工程唯一的BSP层配置文件
 *          集中管理所有外设的引脚/端口/时钟/中断/时序/极性配置,
 *          更换电路板或调整引脚分配时仅需修改本文件
 *
 * 命名约定:
 *   - 芯片驱动沿用器件前缀宏(LCD_/TP_/ICM20602_/MAX30102_/DX24_)
 *   - 板级适配模块使用HW_前缀宏(LED/BEEP/调试串口/节拍定时器/RTC/蓝牙串口)
 *   - 器件内部参数(寄存器地址/I2C地址/缓冲大小)留在各自驱动头文件
 */
#ifndef __HARDWARE_CONFIG_H
#define __HARDWARE_CONFIG_H

#include "stm32f4xx.h"

/* ==================== 1. LED ==================== */
/* LED1: PF9, 低电平点亮 */
#define HW_LED1_PORT          GPIOF
#define HW_LED1_PIN           GPIO_Pin_9
#define HW_LED1_GPIO_CLK      RCC_AHB1Periph_GPIOF
#define HW_LED1_ACTIVE_LEVEL  0

/* LED2: PF10, 低电平点亮 */
#define HW_LED2_PORT          GPIOF
#define HW_LED2_PIN           GPIO_Pin_10
#define HW_LED2_GPIO_CLK      RCC_AHB1Periph_GPIOF
#define HW_LED2_ACTIVE_LEVEL  0

/* ==================== 2. 蜂鸣器 ==================== */
/* BEEP: PF8, 高电平鸣响 */
#define HW_BEEP_PORT          GPIOF
#define HW_BEEP_PIN           GPIO_Pin_8
#define HW_BEEP_GPIO_CLK      RCC_AHB1Periph_GPIOF
#define HW_BEEP_ACTIVE_LEVEL  1

/* ==================== 3. 调试串口 USART1 ==================== */
/* PA9-TX / PA10-RX */
#define HW_DBG_UART_INSTANCE     USART1
#define HW_DBG_UART_APB_BUS      2                        /* APB2总线 */
#define HW_DBG_UART_CLK         RCC_APB2Periph_USART1
#define HW_DBG_UART_BAUDRATE    9600
#define HW_DBG_UART_IRQ_CHANNEL USART1_IRQn
#define HW_DBG_UART_IRQ_PREEMPT 7                        /* >=FreeRTOS临界值5 */
#define HW_DBG_UART_IRQ_SUB     0
#define HW_DBG_TX_PORT          GPIOA
#define HW_DBG_TX_PIN           GPIO_Pin_9
#define HW_DBG_TX_GPIO_CLK      RCC_AHB1Periph_GPIOA
#define HW_DBG_RX_PORT          GPIOA
#define HW_DBG_RX_PIN           GPIO_Pin_10
#define HW_DBG_RX_GPIO_CLK      RCC_AHB1Periph_GPIOA
#define HW_DBG_UART_GPIO_AF     GPIO_AF_USART1

/* ==================== 4. LVGL节拍定时器 TIM3 ==================== */
/* 84MHz / 84分频 = 1MHz, 计数1000 -> 1ms节拍 */
#define HW_TICK_TIM_INSTANCE    TIM3
#define HW_TICK_TIM_CLK         RCC_APB1Periph_TIM3
#define HW_TICK_TIM_PSC         (84 - 1)
#define HW_TICK_TIM_ARR         (1000 - 1)
#define HW_TICK_TIM_IRQ_PREEMPT 0
#define HW_TICK_TIM_IRQ_SUB     1

/* ==================== 5. RTC ==================== */
/* LSE 32.768kHz: 128(异步) x 256(同步) = 32768 -> 1Hz */
#define HW_RTC_ASYNC_PREDIV     (128 - 1)
#define HW_RTC_SYNC_PREDIV      (256 - 1)
#define HW_RTC_BKP_DR           0x13    /* 首次上电标记(备份寄存器DR0) */
#define HW_RTC_ALARM_IRQ_PREEMPT 2
#define HW_RTC_ALARM_IRQ_SUB    2

/* ==================== 6. LCD ST7789 (SPI1) ==================== */
#define LCD_SPI_INSTANCE        SPI1
#define LCD_SPI_CLK             RCC_APB2Periph_SPI1
#define LCD_SPI_GPIO_AF         GPIO_AF_SPI1
#define LCD_SPI_PRESCALER       SPI_BaudRatePrescaler_2   /* 84M/2=42MHz, 保证长线信号完整性 */
#define LCD_SPI_CPOL            SPI_CPOL_High             /* 模式3 */
#define LCD_SPI_CPHA            SPI_CPHA_2Edge

#define LCD_SCL_PORT    GPIOB
#define LCD_SCL_PIN     GPIO_Pin_3      /* PB3 -> SPI1_SCK  (AF5) */
#define LCD_SCL_GPIO_CLK   RCC_AHB1Periph_GPIOB
#define LCD_SDA_PORT    GPIOB
#define LCD_SDA_PIN     GPIO_Pin_5      /* PB5 -> SPI1_MOSI (AF5) */
#define LCD_SDA_GPIO_CLK   RCC_AHB1Periph_GPIOB

#define LCD_RES_PORT    GPIOB
#define LCD_RES_PIN     GPIO_Pin_11     /* PB11 复位 */
#define LCD_RES_GPIO_CLK   RCC_AHB1Periph_GPIOB
#define LCD_CS_PORT     GPIOA
#define LCD_CS_PIN      GPIO_Pin_3      /* PA3  片选 */
#define LCD_CS_GPIO_CLK    RCC_AHB1Periph_GPIOA
#define LCD_DC_PORT     GPIOB
#define LCD_DC_PIN      GPIO_Pin_10     /* PB10 数据/命令 */
#define LCD_DC_GPIO_CLK    RCC_AHB1Periph_GPIOB

/* 背光: TIM2_CH3 -> PA2, 84MHz/84=1MHz, /100=10kHz PWM */
#define LCD_BL_PWM_INSTANCE    TIM2
#define LCD_BL_PWM_CLK         RCC_APB1Periph_TIM2
#define LCD_BL_PWM_PSC         (84 - 1)
#define LCD_BL_PWM_ARR         (100 - 1)
#define LCD_BL_PWM_CHANNEL     3
#define LCD_BL_PWM_GPIO_AF     GPIO_AF_TIM2
#define LCD_BLK_PORT    GPIOA
#define LCD_BLK_PIN     GPIO_Pin_2
#define LCD_BLK_GPIO_CLK   RCC_AHB1Periph_GPIOA

/* LCD刷屏DMA: SPI1_TX = DMA2_Stream3/通道3(F407固定映射), 单次传输+TC中断 */
#define LCD_DMA_INSTANCE       DMA2_Stream3
#define LCD_DMA_CLK            RCC_AHB1Periph_DMA2
#define LCD_DMA_CHANNEL        DMA_Channel_3
#define LCD_DMA_IRQ_CHANNEL    DMA2_Stream3_IRQn
#define LCD_DMA_IRQ_PREEMPT    5     /* =FreeRTOS临界值5, TC中断使用FromISR */
#define LCD_DMA_IRQ_SUB        0

/* ==================== 7. 触摸 CST816 (模拟I2C) ==================== */
#define TP_I2C_SDA_PORT     GPIOB
#define TP_I2C_SDA_PIN      GPIO_Pin_9      /* PB9 数据线 */
#define TP_I2C_SDA_GPIO_CLK    RCC_AHB1Periph_GPIOB
#define TP_I2C_SCL_PORT     GPIOB
#define TP_I2C_SCL_PIN      GPIO_Pin_8      /* PB8 时钟线 */
#define TP_I2C_SCL_GPIO_CLK    RCC_AHB1Periph_GPIOB

/* ==================== 8. 六轴 ICM20602 (模拟I2C) ==================== */
/* PB0 的复用功能为 TIM3_CH3, 本驱动仅作普通GPIO开漏使用
 * (TIM3 已用作 LVGL tick, 不占用GPIO引脚, 无冲突)
 * PC13 为RTC备份域引脚, 内部上拉较弱, 建议外接4.7K上拉电阻 */
#define ICM20602_SCL_PORT    GPIOB
#define ICM20602_SCL_PIN     GPIO_Pin_0      /* PB0 时钟线 */
#define ICM20602_SCL_GPIO_CLK   RCC_AHB1Periph_GPIOB
#define ICM20602_SDA_PORT    GPIOC
#define ICM20602_SDA_PIN     GPIO_Pin_13     /* PC13 数据线 */
#define ICM20602_SDA_GPIO_CLK   RCC_AHB1Periph_GPIOC

/* ==================== 9. 心率血氧 MAX30102 (模拟I2C+中断) ==================== */
/* PC8/PC9 的复用功能为 TIM3_CH3/CH4, 本驱动仅作普通GPIO开漏使用
 * (TIM3 已用作 LVGL tick, 不占用GPIO引脚, 无冲突) */
#define MAX30102_SDA_PORT    GPIOC
#define MAX30102_SDA_PIN     GPIO_Pin_8      /* PC8 数据线 */
#define MAX30102_SDA_GPIO_CLK   RCC_AHB1Periph_GPIOC
#define MAX30102_SCL_PORT    GPIOC
#define MAX30102_SCL_PIN     GPIO_Pin_9      /* PC9 时钟线 */
#define MAX30102_SCL_GPIO_CLK   RCC_AHB1Periph_GPIOC

/* INT为开漏输出低有效, 配EXTI15下降沿, FIFO将满(A_FULL)时通知任务
 * (PA15为JTAG脚, 配置为输入即自动释放JTDI, SWD调试口PA13/PA14不受影响) */
#define MAX30102_INT_PORT    GPIOA
#define MAX30102_INT_PIN     GPIO_Pin_15    /* PA15 中断线 */
#define MAX30102_INT_EXTI_LINE      EXTI_Line15
#define MAX30102_INT_EXTI_PORT_SRC  EXTI_PortSourceGPIOA
#define MAX30102_INT_EXTI_PIN_SRC   EXTI_PinSource15
#define MAX30102_INT_IRQn           EXTI15_10_IRQn

/* ==================== 10. 蓝牙 DX24 (USART6) ==================== */
/* PC6-TX -> 蓝牙RXD, PC7-RX <- 蓝牙TXD */
#define HW_BT_UART_INSTANCE     USART6
#define HW_BT_UART_APB_BUS      2               /* APB2总线 */
#define HW_BT_UART_CLK          RCC_APB2Periph_USART6
#define HW_BT_UART_BAUDRATE     9600
#define HW_BT_UART_IRQ_CHANNEL  USART6_IRQn
#define HW_BT_UART_IRQ_PREEMPT  6     /* >=FreeRTOS临界值5, FromISR可用 */
#define HW_BT_UART_IRQ_SUB      0
#define HW_BT_TX_PORT           GPIOC
#define HW_BT_TX_PIN            GPIO_Pin_6
#define HW_BT_TX_PIN_SOURCE     GPIO_PinSource6
#define HW_BT_TX_GPIO_CLK       RCC_AHB1Periph_GPIOC
#define HW_BT_RX_PORT           GPIOC
#define HW_BT_RX_PIN            GPIO_Pin_7
#define HW_BT_RX_PIN_SOURCE     GPIO_PinSource7
#define HW_BT_RX_GPIO_CLK       RCC_AHB1Periph_GPIOC
#define HW_BT_UART_GPIO_AF      GPIO_AF_USART6

/* 模块STATE脚: 手机连接后输出高电平, 断开为低电平 */
#define DX24_STATE_PORT      GPIOB
#define DX24_STATE_PIN       GPIO_Pin_6     /* PB6 状态输入 */
#define DX24_STATE_GPIO_CLK  RCC_AHB1Periph_GPIOB

#endif /* __HARDWARE_CONFIG_H */
