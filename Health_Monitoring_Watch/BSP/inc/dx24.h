#ifndef __DX24_H
#define __DX24_H

#include "FreeRTOS.h"
#include "task.h"

/* 蓝牙接收缓冲大小(字节) */
#define DX24_RX_BUF_LEN  128

/* 蓝牙接收缓冲(IDLE中断置位, 任务处理后清零) */
extern uint8_t dx_rx_buf[DX24_RX_BUF_LEN];
extern volatile uint16_t dx_rx_len;    /* 本帧有效长度 */
extern volatile uint8_t dx_rx_flag;   /* 1=收到一帧 */

extern void DX24_Init(void);          /* 波特率等板级参数见 hardware_config.h */
extern void DX24_SendStr(uint8_t *str, uint16_t len);
extern void DX24_SetNotifyTask(TaskHandle_t task);
extern uint8_t DX24_IsConnected(void);   /* 1=蓝牙已连接(STATE高) */
extern uint8_t DX24_GetFrame(uint8_t *buf, uint16_t *len);   /* 取一帧: 1=取到(拷贝+清空+关中断保护) */
#endif
