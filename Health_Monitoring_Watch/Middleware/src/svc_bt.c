/**
 * @file    svc_bt.c
 * @brief   Middleware层蓝牙服务实现: 封装DX24驱动
 */
#include "svc_bt.h"
#include "dx24.h"
#include <string.h>

void SVC_BT_SetNotifyTask(TaskHandle_t task)
{
    DX24_SetNotifyTask(task);
}

void SVC_BT_Init(void)
{
    DX24_Init();
}

uint8_t SVC_BT_GetFrame(uint8_t *buf, uint16_t *len)
{
    return DX24_GetFrame(buf, len);
}

void SVC_BT_SendStr(const char *str)
{
    DX24_SendStr((uint8_t *)str, (uint16_t)strlen(str));
}

uint8_t SVC_BT_IsConnected(void)
{
    return DX24_IsConnected();
}
