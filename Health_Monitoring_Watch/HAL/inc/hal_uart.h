/**
 * @file    hal_uart.h
 * @brief   HAL层通用UART驱动: 配置结构体驱动的多实例串口框架
 *          与具体板级硬件无关, 引脚/波特率/中断参数由BSP层注入
 *
 * 接收设计(中断驱动):
 *   - RXNE: 每字节到达, 调用注册的 rx_byte_cb
 *   - IDLE: 一帧结束,   调用注册的 rx_idle_cb
 *   - BSP层在各回调中自行组织缓冲, HAL层不持有业务数据
 *   - 中断向量函数(如USART1_IRQHandler)由BSP层定义,
 *     转调 HAL_UART_IRQHandler(instance) 完成通用分发
 */
#ifndef __HAL_UART_H
#define __HAL_UART_H

#include "stm32f4xx.h"
#include "hal_gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 接收回调类型(中断上下文, 须短小非阻塞) */
typedef void (*HAL_UART_RxByteCb_t)(uint8_t data);
typedef void (*HAL_UART_RxIdleCb_t)(void);

/* UART通用配置结构体(BSP层填充板级参数) */
typedef struct {
    USART_TypeDef       *instance;      /* USART1~USART6 */
    uint8_t              apb_bus;       /* 时钟总线: 1=APB1, 2=APB2 */
    uint32_t             clk;           /* 外设时钟: RCC_APBxPeriph_USARTx */
    uint32_t             baudrate;      /* 波特率 */
    HAL_GPIO_Config_t    tx;             /* TX引脚配置(复用推挽) */
    HAL_GPIO_Config_t    rx;             /* RX引脚配置(复用推挽) */
    uint8_t              irq_channel;   /* NVIC中断通道: USARTx_IRQn */
    uint8_t              irq_preempt;   /* 抢占优先级 */
    uint8_t              irq_sub;       /* 响应优先级 */
} HAL_UART_Config_t;

/* HAL层通用UART接口 */
void HAL_UART_Init(const HAL_UART_Config_t *cfg);                            /* 初始化串口(引脚+外设+NVIC+中断) */
void HAL_UART_SetRxCallback(USART_TypeDef *uart, HAL_UART_RxByteCb_t byte_cb, HAL_UART_RxIdleCb_t idle_cb);  /* 注册接收回调(中断上下文) */
void HAL_UART_SendByte(USART_TypeDef *uart, uint8_t data);                    /* 发送单字节(阻塞至TXE) */
void HAL_UART_SendBuffer(USART_TypeDef *uart, const uint8_t *buf, uint16_t len); /* 发送数据缓冲 */
void HAL_UART_IRQHandler(USART_TypeDef *uart);                               /* 通用中断处理(BSP中断向量函数转调) */

#ifdef __cplusplus
}
#endif

#endif /* __HAL_UART_H */
