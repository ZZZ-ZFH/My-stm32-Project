/**
 * @file    svc_health.h
 * @brief   Middleware层健康数据服务: 心率/血氧采集与算法计算,
 *          对APP屏蔽MAX30102驱动的硬件细节
 */
#ifndef __SVC_HEALTH_H
#define __SVC_HEALTH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 健康数据测量结果(与BSP驱动结构解耦) */
typedef struct {
    int32_t heart_rate;                      /* 心率 bpm */
    int8_t  hr_valid;                        /* 1=心率有效 */
    int32_t spo2;                            /* 血氧 % */
    int8_t  spo2_valid;                      /* 1=血氧有效 */
} svc_health_result_t;

/* 初始化心率血氧传感器, 返回1=成功(失败由内部2s周期重连机制恢复) */
uint8_t SVC_HEALTH_Init(void);

/* 周期采样+算法计算(100ms周期调用), 返回1=result已更新 */
uint8_t SVC_HEALTH_Process(svc_health_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* __SVC_HEALTH_H */
