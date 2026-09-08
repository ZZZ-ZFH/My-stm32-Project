/**
 * @file    svc_bt.h
 * @brief   Middleware层蓝牙服务: 数据收发/连接状态/接收任务通知,
 *          对APP屏蔽DX24驱动的硬件细节
 */
#ifndef __SVC_BT_H
#define __SVC_BT_H

#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 蓝牙一帧最大长度(字节) */
#define SVC_BT_RX_BUF_LEN  128

/* 注册当前任务为接收事件通知目标(须在SVC_BT_Init之前调用) */
void SVC_BT_SetNotifyTask(TaskHandle_t task);

/* 初始化蓝牙模块串口(波特率等板级参数在BSP内部) */
void SVC_BT_Init(void);

/* 取一帧接收数据(拷贝到调用者缓冲并清空接收状态), 返回1=取到 */
uint8_t SVC_BT_GetFrame(uint8_t *buf, uint16_t *len);

/* 发送字符串(阻塞发送) */
void SVC_BT_SendStr(const char *str);

/* 读连接状态: 1=手机已连接 */
uint8_t SVC_BT_IsConnected(void);

#ifdef __cplusplus
}
#endif

#endif /* __SVC_BT_H */
