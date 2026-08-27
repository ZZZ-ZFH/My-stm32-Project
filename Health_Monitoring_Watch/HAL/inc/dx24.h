#ifndef __DX24_H
#define __DX24_H

#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "task.h"

/* 蓝牙接收缓冲大小(字节) */
#define DX24_RX_BUF_LEN  128

/* ==================== 连接状态引脚 ==================== */
/* 模块STATE脚: 手机连接后输出高电平, 断开为低电平 */
#define DX24_STATE_PORT      GPIOB
#define DX24_STATE_PIN       GPIO_Pin_6     /* PB6 状态输入 */

/* 蓝牙接收缓冲(IDLE中断置位, 任务处理后清零) */
extern uint8_t dx_rx_buf[DX24_RX_BUF_LEN];
extern volatile u16     dx_rx_len;    /* 本帧有效长度 */
extern volatile uint8_t dx_rx_flag;   /* 1=收到一帧 */

extern void DX24_Init(u32 baud);
extern void DX24_SendStr(uint8_t *str,u16 len);
extern void DX24_SetNotifyTask(TaskHandle_t task);
extern uint8_t DX24_IsConnected(void);   /* 1=蓝牙已连接(STATE高) */
#endif
