#ifndef __IMU_ALGO_H
#define __IMU_ALGO_H

#include <stdint.h>

/* ==================== 可移植算法层接口 ====================
 * 算法核心(姿态解算/计步)仅依赖本文件的数据结构, 不包含任何
 * 硬件头文件; 换平台时只需重新实现数据采集桥接(见imu_algo.c
 * 底部 IMU_Alg_ 前缀的硬件相关函数), 算法代码零改动
 */

/* 输入: 6轴数据(物理单位, 由采集层负责量程换算) */
typedef struct
{
    float ax, ay, az;      /* 加速度 单位:g */
    float gx, gy, gz;      /* 角速度 单位:deg/s(已去零偏) */
} imu_data_t;

/* 输出: 算法结果 */
typedef struct
{
    float roll;            /* 横滚角 度 */
    float pitch;           /* 俯仰角 度 */
    float yaw;             /* 航向角 度(仅陀螺积分, 会缓慢漂移) */
    uint32_t steps;         /* 累计步数 */
    float cadence;          /* 步速 步/分(0=静止/未检测) */
    uint8_t wrist_raise;    /* 1=本次检测到抬手手势(事件型, 仅置位一帧) */
} imu_result_t;

/* -------------------- 纯算法接口(可移植) -------------------- */
void IMU_Alg_Reset(void);                                  /* 清零姿态与步数 */
void IMU_Alg_Update(const imu_data_t *data, float dt);     /* 更新一次, dt=采样周期秒 */
void IMU_Alg_GetResult(imu_result_t *result);              /* 获取当前结果 */
void IMU_Alg_SetSteps(uint32_t steps);                      /* 设置步数初值(掉电恢复用) */

/* -------------------- 硬件桥接接口(依赖HAL) -------------------- */
/* 初始化传感器(HAL桥接, 内部调用ICM20602_Init), 返回1=成功 */
uint8_t IMU_Alg_Init(void);
/* 采集一帧并运行算法(桥接: 读ICM20602原始值->换算->更新算法)
 * sample_ms=采样周期毫秒(如25ms), 返回1=本次更新有效 */
uint8_t IMU_Alg_Process(uint32_t sample_ms);

#endif
