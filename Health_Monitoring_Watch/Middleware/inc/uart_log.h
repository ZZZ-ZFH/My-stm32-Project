#ifndef __UART_LOG_H
#define __UART_LOG_H

/* printf日志重定向模块(收编自HAL/uart1.c, 修正HAL层承载日志职责
 * 的反向依赖):
 * - printf最终经fputc调用BSP层BSP_UART_SendByte输出到USART1
 * - 使用前须先完成BSP_UART_Init(), 否则fputc等待TXE标志会死循环
 * - 本模块无对外API, 仅提供链接期重定向, 谁链入谁生效 */

#endif
