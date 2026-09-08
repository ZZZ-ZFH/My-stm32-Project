/**
 * @file    uart_log.c
 * @brief   printf重定向到USART1(原HAL/uart1.c中的fputc收编至此)
 *          使用前须先调用BSP_UART_Init(), 见uart_log.h说明
 */
#include "uart_log.h"
#include <stdio.h>
#include "bsp_uart.h"

#pragma import(__use_no_semihosting)   /* 不使用半主机模式 */

/* 标准库需要的支持函数 */
struct __FILE
{
    int handle;
};

FILE __stdout;

/* 定义_sys_exit()以避免使用半主机模式 */
int _sys_exit(int x)
{
    x = x;
    return x;
}

/* 重定义fputc: printf是宏, 最终逐字节经USART1输出 */
int fputc(int ch, FILE *f)
{
    (void)f;
    BSP_UART_SendByte((uint8_t)ch);
    return ch;
}
