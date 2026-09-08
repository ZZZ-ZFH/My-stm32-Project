/**
 * @file    bsp_uart.h
 * @brief   BSP层板级调试串口适配: USART1 (PA9-TX, PA10-RX)
 *          基于HAL_UART通用框架实例化, 提供收发与帧接收接口
 *
 * 板级硬件:
 *   U1_TX PA9  (AF7)
 *   U1_RX PA10 (AF7)
 * 接收流程: RXNE逐字节入帧缓冲, IDLE空闲标志一帧结束
 */
#ifndef __BSP_UART_H
#define __BSP_UART_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 帧接收缓冲大小(与协议层最大帧长度一致) */
#define BSP_UART_RX_BUF_SIZE    135

/* BSP层串口接口(供中间件调用) */
void    BSP_UART_Init(void);                /* 初始化调试串口USART1(波特率见 hardware_config.h) */
void    BSP_UART_SendByte(uint8_t data);    /* 发送单字节(printf重定向使用) */
void    BSP_UART_SendBuffer(const uint8_t *buf, uint16_t len);  /* 发送数据缓冲 */
uint8_t BSP_UART_GetRxFrame(uint8_t *buf, uint16_t *len);       /* 取一帧: 1=取到, 并清空接收状态 */

#ifdef __cplusplus
}
#endif

#endif /* __BSP_UART_H */
