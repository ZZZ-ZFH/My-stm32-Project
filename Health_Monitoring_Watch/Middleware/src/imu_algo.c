/**
 * @file    imu_algo.c
 * @brief   IMU算法层: 姿态解算(互补滤波) + 计步器(峰值检测状态机)
 *
 * 架构说明:
 *   Middleware(本文件) --算法核心--> 纯数据运算, 无硬件依赖, 可移植
 *                         --桥接层----> 直接调用HAL层ICM20602驱动
 *   后续封装BSP层时, 只需将底部"硬件桥接"区替换为BSP接口,
 *   算法核心区(姿态/计步)零改动
 *
 * 算法参数:
 *   姿态: 互补滤波 alpha=0.98(陀螺为主, 加速度修正漂移)
 *         roll/pitch无累积漂移, yaw仅有陀螺积分会缓慢漂移(无磁力计)
 *   计步: 加速度幅值低通滤波 -> 阈值峰值检测状态机
 *         步频窗口0.2~2Hz, 连续步检测
 */
#include "imu_algo.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979f
#endif
#define RAD2DEG(x)  ((x) * 57.29577951f)

/* ==================== 算法内部状态 ==================== */
static imu_result_t alg_result;              /* 输出结果 */
static uint8_t     alg_inited = 0;           /* 姿态已初始化标志 */

/* 互补滤波系数: 越接近1越信任陀螺仪 */
#define CF_ALPHA       0.98f

/* -------------------- 计步器参数 -------------------- */
#define STEP_LPF_ALPHA   0.2f               /* 幅值低通(较小=更平滑) */
#define STEP_THRESH_HIGH 1.1f               /* g, 上升沿触发阈值 */
#define STEP_THRESH_LOW  0.93f              /* g, 下降沿复位阈值 */
#define STEP_MIN_MS      300u              /* 单步最短间隔(防抖动) */
#define STEP_MAX_MS      2000u             /* 单步最长间隔(超时重同步) */
#define STEP_CONT_MIN    3                 /* 连续N步才开始计数(防误报) */

typedef enum { STEP_WAIT_RISE = 0, STEP_WAIT_FALL, STEP_IDLE } step_state_t;
static step_state_t step_state = STEP_WAIT_RISE;
static float    step_mag_lpf   = 1.0f;     /* 加速度幅值低通值 */
static uint32_t step_last_ms   = 0;         /* 上一步时刻(算法帧计数) */
static uint32_t step_frame_ms  = 0;         /* 帧时间累计(毫秒) */
static uint8_t  step_cont_cnt  = 0;         /* 待确认的连续步数 */
static uint32_t step_counted_ms = 0;        /* 上一次计步时刻 */
static float    step_cadence   = 0.0f;      /* 步速 步/分 */

/* ==================== 算法核心: 抬手检测 ==================== */
/* 手表佩戴时抬手动作特征:
 *   1. 短时间内手腕快速翻转 -> 陀螺仪角速度峰值超阈值
 *   2. 随后手臂抬起稳定 -> 重力向量从近似水平变为朝下(Z轴分量增大)
 *   3. 动作时长限制在0.15~1.5秒, 超时判定非抬手(如缓慢转体)
 */
#define WRIST_GYRO_MIN     120.0f          /* deg/s, 角速度触发阈值 */
#define WRIST_GYRO_END     40.0f           /* deg/s, 动作结束阈值 */
#define WRIST_Z_LOW        0.3f            /* 起始Z轴分量上限(手臂下垂) */
#define WRIST_Z_HIGH       0.75f           /* 结束Z轴分量下限(手臂抬起) */
#define WRIST_T_MIN_MS     150u            /* 最短动作时长 */
#define WRIST_T_MAX_MS     1500u           /* 最长动作时长 */
#define WRIST_HOLDOFF_MS   3000u           /* 触发后抑制时间(防连续误触发) */

typedef enum { WRIST_IDLE = 0, WRIST_ACTIVE } wrist_state_t;
static wrist_state_t wrist_state = WRIST_IDLE;
static uint32_t wrist_trig_ms  = 0;         /* 角速度触发时刻 */
static uint32_t wrist_last_evt = 0;         /* 上次事件时刻(抑制用) */

/**
 * @brief  抬手手势检测状态机
 * @note   检测到时在alg_result.wrist_raise置位一帧(事件型输出)
 */
static void IMU_Alg_WristRaise(const imu_data_t *data, float dt)
{
    uint32_t frame_ms = (uint32_t)(dt * 1000.0f + 0.5f);
    float gyro_mag = sqrtf(data->gx * data->gx + data->gy * data->gy
                        + data->gz * data->gz);
    float accel_norm = sqrtf(data->ax * data->ax + data->ay * data->ay
                          + data->az * data->az);
    float az_norm = (accel_norm > 0.01f) ? (data->az / accel_norm) : 1.0f;

    /* 事件抑制窗口 */
    if (wrist_last_evt &&
        (step_frame_ms - wrist_last_evt) < WRIST_HOLDOFF_MS)
        return;

    switch (wrist_state)
    {
    case WRIST_IDLE:
        if (gyro_mag > WRIST_GYRO_MIN && az_norm < WRIST_Z_LOW)
        {
            wrist_state  = WRIST_ACTIVE;
            wrist_trig_ms = step_frame_ms;
        }
        break;

    case WRIST_ACTIVE:
        if (az_norm > WRIST_Z_HIGH)                  /* 手臂已抬起 */
        {
            uint32_t dur = step_frame_ms - wrist_trig_ms;
            if (dur >= WRIST_T_MIN_MS && dur <= WRIST_T_MAX_MS)
            {
                alg_result.wrist_raise = 1;          /* 事件置位一帧 */
                wrist_last_evt = step_frame_ms;
            }
            wrist_state = WRIST_IDLE;
        }
        else if (gyro_mag < WRIST_GYRO_END)          /* 动作停止 */
        {
            uint32_t dur = step_frame_ms - wrist_trig_ms;
            if (dur >= WRIST_T_MIN_MS && dur <= WRIST_T_MAX_MS &&
                az_norm > WRIST_Z_LOW + 0.2f)        /* Z分量有抬升趋势 */
            {
                alg_result.wrist_raise = 1;
                wrist_last_evt = step_frame_ms;
            }
            wrist_state = WRIST_IDLE;
        }
        else if ((step_frame_ms - wrist_trig_ms) > WRIST_T_MAX_MS)
        {
            wrist_state = WRIST_IDLE;               /* 超时放弃 */
        }
        break;

    default:
        wrist_state = WRIST_IDLE;
        break;
    }
}

/* ==================== 算法核心: 姿态解算 ==================== */
/**
 * @brief  互补滤波姿态更新
 * @param  data 6轴数据(g, deg/s)
 * @param  dt   采样周期(秒)
 * @note   首帧以加速度计解算的roll/pitch直接初始化
 */
static void IMU_Alg_Attitude(const imu_data_t *data, float dt)
{
    float ax = data->ax, ay = data->ay, az = data->az;
    float norm = sqrtf(ax * ax + ay * ay + az * az);

    /* 加速度计模长在0.5~2g之间才可信(排除剧烈运动段) */
    if (norm > 0.5f && norm < 2.0f)
    {
        float acc_roll  = atan2f(ay, az);
        float acc_pitch = atan2f(-ax, sqrtf(ay * ay + az * az));

        if (!alg_inited)
        {
            alg_result.roll  = RAD2DEG(acc_roll);
            alg_result.pitch = RAD2DEG(acc_pitch);
            alg_result.yaw   = 0.0f;
            alg_inited       = 1;
            return;
        }

        /* 陀螺积分(先转成欧拉角角速度的近似) */
        float gr = data->gx, gp = data->gy, gy = data->gz;
        float roll_rad  = alg_result.roll  * (M_PI / 180.0f);
        float pitch_rad = alg_result.pitch * (M_PI / 180.0f);

        float d_roll  = gr + sinf(roll_rad) * tanf(pitch_rad) * gp
                          + cosf(roll_rad) * tanf(pitch_rad) * gy;
        float d_pitch = cosf(roll_rad) * gp - sinf(roll_rad) * gy;

        /* 互补滤波: 陀螺积分 + 加速度修正 */
        alg_result.roll  = CF_ALPHA * (alg_result.roll  + d_roll  * dt)
                         + (1.0f - CF_ALPHA) * RAD2DEG(acc_roll);
        alg_result.pitch = CF_ALPHA * (alg_result.pitch + d_pitch * dt)
                         + (1.0f - CF_ALPHA) * RAD2DEG(acc_pitch);
    }
    else if (alg_inited)
    {
        /* 剧烈运动段: 仅陀螺积分 */
        float gr = data->gx, gp = data->gy, gy = data->gz;
        float roll_rad  = alg_result.roll  * (M_PI / 180.0f);
        float pitch_rad = alg_result.pitch * (M_PI / 180.0f);

        alg_result.roll  += (gr + sinf(roll_rad) * tanf(pitch_rad) * gp
                                + cosf(roll_rad) * tanf(pitch_rad) * gy) * dt;
        alg_result.pitch += (cosf(roll_rad) * gp - sinf(roll_rad) * gy) * dt;
    }

    /* yaw无外部参考, 纯积分(会缓慢漂移, 手表应用一般不用) */
    alg_result.yaw += data->gz * dt;

    /* 角度归一化到±180° */
    if (alg_result.roll  >  180.0f) alg_result.roll  -= 360.0f;
    if (alg_result.roll  < -180.0f) alg_result.roll  += 360.0f;
    if (alg_result.pitch >  180.0f) alg_result.pitch -= 360.0f;
    if (alg_result.pitch < -180.0f) alg_result.pitch += 360.0f;
}

/* ==================== 算法核心: 计步器 ==================== */
/**
 * @brief  计步状态机: 幅值低通->双阈值峰值检测
 * @param  data 6轴数据, dt 采样周期(秒)
 * @note   连续STEP_CONT_MIN步才计入总数, 消除偶发误触发
 */
static void IMU_Alg_Pedometer(const imu_data_t *data, float dt)
{
    (void)dt;
    uint32_t frame_ms = (uint32_t)(dt * 1000.0f + 0.5f);

    step_frame_ms += frame_ms;

    /* 总加速度幅值低通滤波 */
    float mag = sqrtf(data->ax * data->ax + data->ay * data->ay
                    + data->az * data->az);
    step_mag_lpf += STEP_LPF_ALPHA * (mag - step_mag_lpf);

    switch (step_state)
    {
    case STEP_WAIT_RISE:                          /* 等待上升沿 */
        if (step_mag_lpf > STEP_THRESH_HIGH)
        {
            if (step_last_ms == 0 ||
                (step_frame_ms - step_last_ms) > STEP_MIN_MS)
            {
                step_last_ms = step_frame_ms;
                step_state   = STEP_WAIT_FALL;
            }
        }
        else if (step_last_ms && (step_frame_ms - step_last_ms) > STEP_MAX_MS)
        {
            step_cont_cnt = 0;                    /* 间隔太久, 重新同步 */
            step_last_ms  = 0;
        }
        break;

    case STEP_WAIT_FALL:                          /* 等待下降沿 */
        if (step_mag_lpf < STEP_THRESH_LOW)
        {
            step_cont_cnt++;
            if (step_cont_cnt >= STEP_CONT_MIN)   /* 连续步确认 */
            {
                alg_result.steps++;
                /* 步速: 由相邻计步间隔换算(60000ms/间隔), 低通平滑 */
                if (step_counted_ms != 0)
                {
                    uint32_t interval = step_frame_ms - step_counted_ms;
                    if (interval >= STEP_MIN_MS && interval <= STEP_MAX_MS)
                    {
                        float cad_new = 60000.0f / (float)interval;
                        step_cadence += 0.3f * (cad_new - step_cadence);
                    }
                }
                step_counted_ms = step_frame_ms;
            }
            step_state = STEP_WAIT_RISE;
        }
        break;

    default:
        step_state = STEP_WAIT_RISE;
        break;
    }

    /* 静止判定: 超过2倍最大步间隔未计步, 步速渐降为0 */
    if (step_counted_ms != 0 &&
        (step_frame_ms - step_counted_ms) > 2 * STEP_MAX_MS)
        step_cadence *= 0.95f;

    alg_result.cadence = step_cadence;
}

/* ==================== 对外: 纯算法接口 ==================== */
void IMU_Alg_Reset(void)
{
    alg_result.roll  = 0.0f;
    alg_result.pitch = 0.0f;
    alg_result.yaw        = 0.0f;
    alg_result.steps      = 0;
    alg_result.cadence    = 0.0f;
    alg_result.wrist_raise = 0;
    alg_inited            = 0;

    wrist_state    = WRIST_IDLE;
    wrist_trig_ms  = 0;
    wrist_last_evt = 0;

    step_state     = STEP_WAIT_RISE;
    step_mag_lpf   = 1.0f;
    step_last_ms   = 0;
    step_frame_ms  = 0;
    step_cont_cnt  = 0;
    step_counted_ms = 0;
    step_cadence   = 0.0f;
}

void IMU_Alg_Update(const imu_data_t *data, float dt)
{
    alg_result.wrist_raise = 0;                /* 事件型: 每帧先清零 */
    IMU_Alg_Attitude(data, dt);
    IMU_Alg_Pedometer(data, dt);
    IMU_Alg_WristRaise(data, dt);
}

void IMU_Alg_GetResult(imu_result_t *result)
{
    *result = alg_result;
}

/* ============================================================
 *                 硬件桥接层(依赖HAL, 可整体替换)
 *  后续封装BSP: 将此区域替换为 bsp_imu.c 的接口调用即可
 * ============================================================ */
#include "icm20602.h"
#include "delay.h"

/* 量程换算: 默认配置±2g/±250dps (16384 LSB/g, 131 LSB/(deg/s)) */
#define ACCEL_LSB_PER_G    16384.0f
#define GYRO_LSB_PER_DPS  131.0f

uint8_t IMU_Alg_Init(void)
{
    IMU_Alg_Reset();

    /* 桥接: 调用HAL层初始化(内部含陀螺零偏校准, 需静止) */
    if (ICM20602_Init() == 0)
        return 0;
    return 1;
}

uint8_t IMU_Alg_Process(uint32_t sample_ms)
{
    icm20602_raw_t raw;
    imu_data_t     data;

    /* 桥接: 调用HAL层读取原始数据并换算物理单位 */
    ICM20602_ReadRaw(&raw);

    data.ax = (float)raw.ax / ACCEL_LSB_PER_G;
    data.ay = (float)raw.ay / ACCEL_LSB_PER_G;
    data.az = (float)raw.az / ACCEL_LSB_PER_G;
    data.gx = (float)raw.gx / GYRO_LSB_PER_DPS;
    data.gy = (float)raw.gy / GYRO_LSB_PER_DPS;
    data.gz = (float)raw.gz / GYRO_LSB_PER_DPS;

    IMU_Alg_Update(&data, (float)sample_ms / 1000.0f);
    return 1;
}
