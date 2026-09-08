/**
 * @file    max30102.c
 * @brief   MAX30102 心率血氧传感器板级驱动: 引脚配置集中于 hardware_config.h,
 *          GPIO初始化经由 HAL 层(hal_gpio), 软件I2C时序保留在本驱动
 *          硬件: 软件I2C PC8=SDA PC9=SCL (开漏+上拉), INT=PA15(未使用)
 *          心率/血氧算法来自 Maxim MAXREFDES117 (Common/algorithm.c)
 * @note    采样100sps, 任务100ms周期轮询读FIFO(不溢出: FIFO深32=320ms)
 */
#include "max30102.h"
#include "algorithm.h"
#include "hal_delay.h"
#include "hal_gpio.h"
#include "hardware_config.h"   /* 引脚配置(MAX30102_SDA/SCL/INT_*) */
#include "string.h"
#include "stdio.h"

/* ==================== 采样配置 ==================== */
#define SAMPLE_RATE_HZ    100                          /* 采样率 */
#define BUFFER_LEN        (SAMPLE_RATE_HZ * 5)         /* 500样本=5秒 */
#define SAMPLES_PER_CALC  SAMPLE_RATE_HZ               /* 每次重算补充样本数 */

/* ---- 有效性过滤 ---- */
/* 接触检测: IR直流低于该值判定"手指未放置"(环境光噪声), 需现场标定 */
#define IR_TOUCH_THRESHOLD   50000
/* 生理范围: 超出判定算法误检(参考算法valid标志不可靠) */
#define HR_MIN               40
#define HR_MAX               200
#define SPO2_MIN             70
#define SPO2_MAX             100

/* ==================== 模拟I2C 位操作(经HAL层) ==================== */
#define SDA_H()     HAL_GPIO_WritePin(MAX30102_SDA_PORT, MAX30102_SDA_PIN, 1)
#define SDA_L()     HAL_GPIO_WritePin(MAX30102_SDA_PORT, MAX30102_SDA_PIN, 0)
#define SCL_H()     HAL_GPIO_WritePin(MAX30102_SCL_PORT, MAX30102_SCL_PIN, 1)
#define SCL_L()     HAL_GPIO_WritePin(MAX30102_SCL_PORT, MAX30102_SCL_PIN, 0)
#define SDA_READ()  HAL_GPIO_ReadPin(MAX30102_SDA_PORT, MAX30102_SDA_PIN)

/* I2C 时序延时(约300kHz) */
#define I2C_DELAY()   { volatile uint16_t t = 30; while (t--); }

/* 等待SCL实际变高: 开漏输出下读IDR即总线真实电平 */
static void MAX30102_I2C_SclWait(void)
{
    uint16_t t = 500;
    while (HAL_GPIO_ReadPin(MAX30102_SCL_PORT,
           MAX30102_SCL_PIN) == 0 && t--)
        ;
}

/* I2C总线卡死自恢复: 事务中途被打断/时序错乱后, 从设备可能停留在
 * 半字节状态并持续拉低SDA, 导致后续START永远无法被识别(表现: 一律NACK)。
 * 处理: 重新初始化GPIO, 发最多9个SCL时钟让从设备走完残余时序释放SDA,
 * 最后补一个STOP条件。恢复失败也无副作用(下一次START重试)。 */
static void MAX30102_I2C_GpioInit(void);   /* 前向声明 */
static void MAX30102_I2C_BusRecover(void)
{
    uint8_t i;

    MAX30102_I2C_GpioInit();     /* SDA/SCL释放为开漏高 */

    for (i = 0; i < 9; i++)      /* 9个时钟脉冲: 覆盖一个完整字节+ACK */
    {
        SCL_L(); I2C_DELAY();
        SCL_H(); MAX30102_I2C_SclWait(); I2C_DELAY();
        if (SDA_READ() != 0)
            break;               /* 从设备已释放SDA */
    }

    /* STOP: SCL高电平期间SDA由低到高 */
    SDA_L(); I2C_DELAY();
    SCL_H(); MAX30102_I2C_SclWait(); I2C_DELAY();
    SDA_H(); I2C_DELAY();
}

/* -------------------- GPIO 初始化(经HAL层) -------------------- */
static void MAX30102_I2C_GpioInit(void)
{
    /* SDA / SCL: 开漏+上拉 */
    static const HAL_GPIO_Config_t s_sda = {
        .port  = MAX30102_SDA_PORT,
        .pin   = MAX30102_SDA_PIN,
        .clk   = MAX30102_SDA_GPIO_CLK,
        .mode  = GPIO_Mode_OUT,
        .otype = GPIO_OType_OD,
        .speed = GPIO_Speed_100MHz,
        .pull  = GPIO_PuPd_UP,
        .af    = 0,
    };
    static const HAL_GPIO_Config_t s_scl = {
        .port  = MAX30102_SCL_PORT,
        .pin   = MAX30102_SCL_PIN,
        .clk   = MAX30102_SCL_GPIO_CLK,
        .mode  = GPIO_Mode_OUT,
        .otype = GPIO_OType_OD,
        .speed = GPIO_Speed_100MHz,
        .pull  = GPIO_PuPd_UP,
        .af    = 0,
    };

    HAL_GPIO_Init(&s_sda);
    HAL_GPIO_Init(&s_scl);

    SDA_H();
    SCL_H();
}

/* -------------------- I2C 起止/字节 -------------------- */
static void MAX30102_I2C_Start(void)
{
    SDA_H(); SCL_H(); MAX30102_I2C_SclWait(); I2C_DELAY();
    SDA_L(); I2C_DELAY();
    SCL_L(); I2C_DELAY();
}

static void MAX30102_I2C_Stop(void)
{
    SDA_L(); SCL_L(); I2C_DELAY();
    SCL_H(); MAX30102_I2C_SclWait(); I2C_DELAY();
    SDA_H(); I2C_DELAY();
}

/* 发送一字节, 返回1=无应答 */
static uint8_t MAX30102_I2C_SendByte(uint8_t dat)
{
    uint8_t ack = 0, i;

    for (i = 0; i < 8; i++)
    {
        if (dat & 0x80) SDA_H(); else SDA_L();
        dat <<= 1;
        I2C_DELAY();
        SCL_H(); MAX30102_I2C_SclWait(); I2C_DELAY();
        SCL_L(); I2C_DELAY();
    }
    /* 读取ACK */
    SDA_H(); I2C_DELAY();
    SCL_H(); MAX30102_I2C_SclWait(); I2C_DELAY();
    if (SDA_READ()) ack = 1;
    SCL_L(); I2C_DELAY();
    return ack;
}

/* 接收一字节, ack=1时回NACK(最后一字节) */
static uint8_t MAX30102_I2C_ReadByte(uint8_t nack)
{
    uint8_t dat = 0, i;

    SDA_H();
    for (i = 0; i < 8; i++)
    {
        dat <<= 1;
        SCL_H(); MAX30102_I2C_SclWait(); I2C_DELAY();
        if (SDA_READ()) dat |= 1;
        SCL_L(); I2C_DELAY();
    }
    if (nack) SDA_H(); else SDA_L();   /* nack=1 -> NACK */
    I2C_DELAY();
    SCL_H(); MAX30102_I2C_SclWait(); I2C_DELAY();
    SCL_L(); SDA_H(); I2C_DELAY();
    return dat;
}

/* ==================== 寄存器读写 ==================== */
/* 连续读寄存器, 返回0=成功; NACK时恢复总线(防从设备卡死SDA) */
static uint8_t MAX30102_ReadRegs(uint8_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t i;

    MAX30102_I2C_Start();
    if (MAX30102_I2C_SendByte(MAX30102_I2C_ADDR << 1))
        { MAX30102_I2C_Stop(); MAX30102_I2C_BusRecover(); return 1; }
    if (MAX30102_I2C_SendByte(reg))
        { MAX30102_I2C_Stop(); MAX30102_I2C_BusRecover(); return 1; }
    MAX30102_I2C_Start();      /* 重复起始 */
    if (MAX30102_I2C_SendByte((MAX30102_I2C_ADDR << 1) | 1))
        { MAX30102_I2C_Stop(); MAX30102_I2C_BusRecover(); return 1; }
    for (i = 0; i < len; i++)
        buf[i] = MAX30102_I2C_ReadByte(i == (len - 1) ? 1 : 0);
    MAX30102_I2C_Stop();
    return 0;
}

/* 写寄存器, 返回0=成功; NACK时恢复总线(防从设备卡死SDA) */
static uint8_t MAX30102_WriteReg(uint8_t reg, uint8_t dat)
{
    MAX30102_I2C_Start();
    if (MAX30102_I2C_SendByte(MAX30102_I2C_ADDR << 1))
        { MAX30102_I2C_Stop(); MAX30102_I2C_BusRecover(); return 1; }
    if (MAX30102_I2C_SendByte(reg))
        { MAX30102_I2C_Stop(); MAX30102_I2C_BusRecover(); return 1; }
    if (MAX30102_I2C_SendByte(dat))
        { MAX30102_I2C_Stop(); MAX30102_I2C_BusRecover(); return 1; }
    MAX30102_I2C_Stop();
    return 0;
}

/* 读单寄存器 */
static uint8_t MAX30102_ReadReg(uint8_t reg)
{
    uint8_t val = 0;

    (void)MAX30102_ReadRegs(reg, &val, 1);
    return val;
}

/* FIFO待读样本数 = (写指针-读指针) & 0x1F */
static uint8_t MAX30102_PendingCount(void)
{
    uint8_t wr = MAX30102_ReadReg(REG_FIFO_WR_PTR);
    uint8_t rd = MAX30102_ReadReg(REG_FIFO_RD_PTR);

    return (uint8_t)((wr - rd) & 0x1F);
}

/* 读一个FIFO样本(红光+红外各18bit, 6字节), 返回0=成功
 * 注: 中断状态由调用方(MAX30102_Process)读清 */
static uint8_t MAX30102_ReadFifoSample(uint32_t *red, uint32_t *ir)
{
    uint8_t buf[6];

    if (MAX30102_ReadRegs(REG_FIFO_DATA, buf, 6) != 0)
        return 1;

    *red = ((uint32_t)(buf[0] & 0x03) << 16) |
           ((uint32_t)buf[1] << 8) | (uint32_t)buf[2];
    *ir  = ((uint32_t)(buf[3] & 0x03) << 16) |
           ((uint32_t)buf[4] << 8) | (uint32_t)buf[5];
    return 0;
}

/* ==================== 对外 API ==================== */
/* 传感器寄存器配置(Init与自愈恢复共用): FIFO/模式/LED电流 */
static void MAX30102_Configure(void)
{
    /* 中断使能关闭(INT引脚未使用, 任务按100ms周期轮询FIFO);
     * 读FIFO不依赖中断, 每轮读走全部待读样本 */
    (void)MAX30102_WriteReg(REG_INTR_ENABLE_1, 0x00);
    (void)MAX30102_WriteReg(REG_INTR_ENABLE_2, 0x00);
    (void)MAX30102_WriteReg(REG_FIFO_WR_PTR, 0x00);    /* 复位FIFO指针 */
    (void)MAX30102_WriteReg(REG_OVF_COUNTER, 0x00);
    (void)MAX30102_WriteReg(REG_FIFO_RD_PTR, 0x00);
    (void)MAX30102_WriteReg(REG_FIFO_CONFIG, 0x0F);    /* 1:1平均,满17中断 */
    (void)MAX30102_WriteReg(REG_MODE_CONFIG, 0x03);    /* SpO2模式 */
    (void)MAX30102_WriteReg(REG_SPO2_CONFIG, 0x27);    /* 4096nA,100sps,18bit */
    (void)MAX30102_WriteReg(REG_LED1_PA, 0x24);        /* 红光约7mA */
    (void)MAX30102_WriteReg(REG_LED2_PA, 0x24);        /* 红外约7mA */
    (void)MAX30102_WriteReg(REG_PILOT_PA, 0x7F);       /* Pilot约25mA */
}

uint8_t MAX30102_Init(void)
{
    uint8_t part_id;

    MAX30102_I2C_GpioInit();

    /* 复位 */
    (void)MAX30102_WriteReg(REG_MODE_CONFIG, 0x40);
    HAL_Delay_Ms(10);

    /* 器件ID固定0x15, 用于确认I2C通信正常 */
    part_id = MAX30102_ReadReg(REG_PART_ID);
    if (part_id != 0x15)
        return 0;

    MAX30102_Configure();

    return 1;
}

/* ---- 采样缓冲(静态, 避免任务栈溢出) ---- */
static uint32_t s_ir_buffer[BUFFER_LEN];   /* 红外数据 */
static uint32_t s_red_buffer[BUFFER_LEN];  /* 红光数据 */
static uint16_t s_sample_idx;              /* 当前写入位置 */

/* 任务周期调用(100ms): 读FIFO全部待读样本并累积,
 * 满BUFFER_LEN计算一次, 返回1=本次完成一轮计算, result已更新
 * 自愈: 采样永不停歇(100sps), 连续1s取不到样本说明总线卡死/模式掉电,
 * 自动恢复总线并重新配置传感器(睡眠唤醒等场景的偶发故障兜底)
 * 自动重连: 快速自愈失败(传感器深度失联)则进入静默期,
 * 期间不访问总线不输出数据, 到期自动完整重连(GPIO重建+总线恢复+
 * 软复位+重配置); 失败则再次静默, 无限重试, 无需人工干预 */
#define SELF_HEAL_EMPTY_ROUNDS   10          /* 连续空轮数(约1s)触发自愈 */
#define FAULT_RETRY_PERIOD_MS    2000        /* 故障重连周期(2秒一次) */

static uint8_t    s_empty_rounds;            /* 连续无样本轮次计数 */
static TickType_t s_retry_at = 0;            /* 重连时刻, 0=非故障态 */

/* 完整重连: GPIO重建+总线恢复+传感器软复位+重配置
 * 返回PART_ID读取值(0x15=成功) */
static uint8_t MAX30102_Reconnect(uint16_t reset_delay_ms)
{
    uint8_t part_id = 0;

    MAX30102_I2C_GpioInit();
    MAX30102_I2C_BusRecover();
    (void)MAX30102_WriteReg(REG_MODE_CONFIG, 0x40);   /* 软复位传感器 */
    /* 复位等待让出CPU(HAL_Delay_Ms会挂起调度器100ms, 卡顿LVGL) */
    vTaskDelay(pdMS_TO_TICKS(reset_delay_ms));
    part_id = MAX30102_ReadReg(REG_PART_ID);
    if (part_id == 0x15)
    {
        MAX30102_Configure();               /* 重配FIFO/模式/LED */
        s_sample_idx = 0;                    /* 丢弃故障前后拼接的旧数据 */
    }
    return part_id;
}

uint8_t MAX30102_Process(max30102_result_t *result)
{
    uint8_t pending, i;
    uint8_t calc_done = 0;
    uint8_t part_id;

    /* 故障静默期: 到期自动重连, 未到期不发任何I2C/数据 */
    if (s_retry_at != 0)
    {
        if ((int32_t)(xTaskGetTickCount() - s_retry_at) < 0)
        {
            s_empty_rounds = 0;             /* 静默期不计空轮 */
            return 0;
        }
        s_retry_at = 0;
        part_id = MAX30102_Reconnect(100);
        printf("MAX30102: reconnect, PART_ID=0x%02X\r\n", (int)part_id);
        if (part_id == 0x15)
        {
            printf("MAX30102: reconnected\r\n");
        }
        else
        {
            s_retry_at = xTaskGetTickCount()
                       + pdMS_TO_TICKS(FAULT_RETRY_PERIOD_MS);
            return 0;                        /* 重连失败: 再静默重试 */
        }
    }

    pending = MAX30102_PendingCount();
    if (pending > MAX30102_FIFO_DEPTH)
        pending = MAX30102_FIFO_DEPTH;

    if (pending == 0)
    {
        /* 传感器正常时每轮都有新样本(100sps, 任务100ms轮询);
         * ReadReg通信失败也返回0 -> pending=0, 同样被此计数捕获 */
        if (++s_empty_rounds >= SELF_HEAL_EMPTY_ROUNDS)
        {
            s_empty_rounds = 0;
            printf("MAX30102: no samples, bus recover...\r\n");
            part_id = MAX30102_Reconnect(10);   /* 快速自愈: 软复位+重配置 */
            if (part_id == 0x15)
            {
                printf("MAX30102: recovered\r\n");
            }
            else
            {
                printf("MAX30102: PART_ID=0x%02X, retry in %ds\r\n",
                       (int)part_id, FAULT_RETRY_PERIOD_MS / 1000);
                s_retry_at = xTaskGetTickCount()
                           + pdMS_TO_TICKS(FAULT_RETRY_PERIOD_MS);
            }
        }
        return 0;
    }
    s_empty_rounds = 0;

    for (i = 0; i < pending; i++)
    {
        if (s_sample_idx >= BUFFER_LEN)
            break;  /* 缓冲满, 剩余样本留在FIFO等下轮 */

        if (MAX30102_ReadFifoSample(&s_red_buffer[s_sample_idx],
                                    &s_ir_buffer[s_sample_idx]) != 0)
            break;  /* 通信失败, 本轮放弃(下轮重试/触发自愈) */

        s_sample_idx++;
    }

    if (s_sample_idx >= BUFFER_LEN)
    {
        int32_t hr, spo2;
        int8_t  hr_valid, spo2_valid;
        uint32_t ir_sum = 0;
        uint16_t k;

        maxim_heart_rate_and_oxygen_saturation(
            s_ir_buffer, BUFFER_LEN, s_red_buffer,
            &spo2, &spo2_valid, &hr, &hr_valid);

        /* 接触检测+生理范围过滤: 参考算法的valid标志不可靠
         * (无手指时噪声也会输出伪心率且标valid=1) */
        for (k = 0; k < BUFFER_LEN; k++)
            ir_sum += s_ir_buffer[k];              /* 500样本求和 */
        if (ir_sum / BUFFER_LEN < IR_TOUCH_THRESHOLD)
            hr_valid = spo2_valid = 0;             /* 未放置手指 */
        else
        {
            if (hr < HR_MIN || hr > HR_MAX)        hr_valid   = 0;
            if (spo2 < SPO2_MIN || spo2 > SPO2_MAX) spo2_valid = 0;
        }

        if (result != NULL)
        {
            result->heart_rate = hr;
            result->hr_valid   = hr_valid;
            result->spo2       = spo2;
            result->spo2_valid = spo2_valid;
        }

        /* 保留最后SAMPLES_PER_CALC个样本移到头部, 下轮补充后重算 */
        memmove(s_ir_buffer,
                &s_ir_buffer[BUFFER_LEN - SAMPLES_PER_CALC],
                SAMPLES_PER_CALC * sizeof(uint32_t));
        memmove(s_red_buffer,
                &s_red_buffer[BUFFER_LEN - SAMPLES_PER_CALC],
                SAMPLES_PER_CALC * sizeof(uint32_t));
        s_sample_idx = SAMPLES_PER_CALC;

        calc_done = 1;
    }
    return calc_done;
}
