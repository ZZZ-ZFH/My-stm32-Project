#ifndef __DX24_H
#define __DX24_H

#include "stm32f4xx.h"
#include "protocol.h"
#include "delay.h"
#include "led.h"

extern uint8_t rx_buf[MAX_FRAME_LEN] ;
extern u16  rx_index ;
extern uint8_t  rx_flag ;

extern void DX24_Init(u32 baud);
extern void DX24_Demo();
extern void DX24_SendStr(uint8_t *str,u16 len);
#endif