/**
 * @file    svc_health.c
 * @brief   Middleware层健康数据服务实现: 封装MAX30102驱动
 */
#include "svc_health.h"
#include "max30102.h"

uint8_t SVC_HEALTH_Init(void)
{
    return MAX30102_Init();
}

uint8_t SVC_HEALTH_Process(svc_health_result_t *result)
{
    max30102_result_t raw;

    if (MAX30102_Process(&raw) != 1)
    {
        return 0;
    }

    result->heart_rate = raw.heart_rate;
    result->hr_valid   = raw.hr_valid;
    result->spo2       = raw.spo2;
    result->spo2_valid = raw.spo2_valid;
    return 1;
}
