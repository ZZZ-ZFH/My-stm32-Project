#ifndef __UART1_H
#define __UART1_H

#include "stm32f4xx.h"
#include "led.h"
#include "protocol.h"

extern uint8_t rx_buf[MAX_FRAME_LEN] ;
extern u16  rx_index ;
extern uint8_t  rx_flag ;
extern void Uart1_Init(u32 baud);
extern void Uart1_SendStr(uint8_t *str,u16 len);
#endif