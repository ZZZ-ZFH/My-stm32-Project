/**
 * @file    max30102.c
 * @brief   MAX30102 心率血氧传感器驱动(标准库版本), 由模块资料参考代码移植
 *          硬件: 软件I2C PC8=SDA PC9=SCL (开漏+上拉), INT=PA15 (EXTI15下降沿)
 *          INT中断: FIFO将满(A_FULL, 阈值17样本)时EXTI通知任务读FIFO,
 *          无数据时任务阻塞, 不轮询总线
 * @note    采样100sps, INT约每170ms触发一次; 任务纯中断驱动无轮询;
 *          心率/血氧算法来自 Maxim MAXREFDES117 (algorithm.c)
 */
#include "max30102.h"
#include "algorithm.h"
#include "delay.h"
#include <string.h>

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

/* ==================== 模拟I2C 位操作 ==================== */
#define SDA_H()     GPIO_SetBits(MAX30102_SDA_PORT, MAX30102_SDA_PIN)
#define SDA_L()     GPIO_ResetBits(MAX30102_SDA_PORT, MAX30102_SDA_PIN)
#define SCL_H()     GPIO_SetBits(MAX30102_SCL_PORT, MAX30102_SCL_PIN)
#define SCL_L()     GPIO_ResetBits(MAX30102_SCL_PORT, MAX30102_SCL_PIN)
#define SDA_READ()  GPIO_ReadInputDataBit(MAX30102_SDA_PORT, MAX30102_SDA_PIN)

/* I2C 时序延时(约300kHz) */
#define I2C_DELAY()   { volatile uint16_t t = 30; while (t--); }

/* 等待SCL实际变高: 开漏输出下读IDR即总线真实电平 */
static void MAX30102_I2C_SclWait(void)
{
    uint16_t t = 500;
    while (GPIO_ReadInputDataBit(MAX30102_SCL_PORT,
           MAX30102_SCL_PIN) == Bit_RESET && t--)
        ;
}

/* -------------------- GPIO 初始化 -------------------- */
static void MAX30102_I2C_GpioInit(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);

    /* SDA / SCL: 开漏+上拉 */
    GPIO_InitStructure.GPIO_Pin   = MAX30102_SDA_PIN | MAX30102_SCL_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_OD;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_Init(MAX30102_SDA_PORT, &GPIO_InitStructure);

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
/* 连续读寄存器, 返回0=成功 */
static uint8_t MAX30102_ReadRegs(uint8_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t i;

    MAX30102_I2C_Start();
    if (MAX30102_I2C_SendByte(MAX30102_I2C_ADDR << 1))
        { MAX30102_I2C_Stop(); return 1; }
    if (MAX30102_I2C_SendByte(reg))
        { MAX30102_I2C_Stop(); return 1; }
    MAX30102_I2C_Start();      /* 重复起始 */
    if (MAX30102_I2C_SendByte((MAX30102_I2C_ADDR << 1) | 1))
        { MAX30102_I2C_Stop(); return 1; }
    for (i = 0; i < len; i++)
        buf[i] = MAX30102_I2C_ReadByte(i == (len - 1) ? 1 : 0);
    MAX30102_I2C_Stop();
    return 0;
}

/* 写寄存器, 返回0=成功 */
static uint8_t MAX30102_WriteReg(uint8_t reg, uint8_t dat)
{
    MAX30102_I2C_Start();
    if (MAX30102_I2C_SendByte(MAX30102_I2C_ADDR << 1))
        { MAX30102_I2C_Stop(); return 1; }
    if (MAX30102_I2C_SendByte(reg))
        { MAX30102_I2C_Stop(); return 1; }
    if (MAX30102_I2C_SendByte(dat))
        { MAX30102_I2C_Stop(); return 1; }
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

/* ==================== INT中断(PC11/EXTI11) ==================== */
/* 接收INT事件的任务句柄, 由MAX30102_SetNotifyTask在Init前注册 */
static TaskHandle_t s_notify_task = NULL;

/* INT引脚+EXTI初始化: 下降沿触发(开漏低有效) */
static void MAX30102_IntInit(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    EXTI_InitTypeDef EXTI_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    /* PA15: F4无需SWJ重映射, 配置为普通输入即自动释放JTDI
     * (SWD调试口在PA13/PA14, 不受影响; INT开漏输出, MCU内部上拉
     * 保证高电平可识别, 模块自身上拉在1.8V域) */
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    GPIO_InitStructure.GPIO_Pin  = MAX30102_INT_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(MAX30102_INT_PORT, &GPIO_InitStructure);

    /* EXTI线15映射到PA15, 下降沿触发 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);
    SYSCFG_EXTILineConfig(MAX30102_INT_EXTI_PORT_SRC,
                           MAX30102_INT_EXTI_PIN_SRC);
    EXTI_InitStructure.EXTI_Line    = MAX30102_INT_EXTI_LINE;
    EXTI_InitStructure.EXTI_Mode    = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    /* 优先级6: 数值须>=FreeRTOS临界值5, 否则FromISR API不允许
     * (数值越大优先级越低, 低于UART1的7) */
    NVIC_InitStructure.NVIC_IRQChannel = MAX30102_INT_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 6;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

/* ISR只做通知, 不在中断里跑软件I2C(含阻塞延时) */
void EXTI15_10_IRQHandler(void)
{
    BaseType_t higher_woken = pdFALSE;

    if (EXTI_GetITStatus(MAX30102_INT_EXTI_LINE) != RESET)
    {
        EXTI_ClearITPendingBit(MAX30102_INT_EXTI_LINE);
        if (s_notify_task != NULL)
        {
            vTaskNotifyGiveFromISR(s_notify_task, &higher_woken);
            portYIELD_FROM_ISR(higher_woken);
        }
    }
}

/* ==================== 对外 API ==================== */
uint8_t MAX30102_Init(void)
{
    uint8_t part_id;

    MAX30102_I2C_GpioInit();

    /* 复位 */
    (void)MAX30102_WriteReg(REG_MODE_CONFIG, 0x40);
    delay_ms(10);

    /* 器件ID固定0x15, 用于确认I2C通信正常 */
    part_id = MAX30102_ReadReg(REG_PART_ID);
    if (part_id != 0x15)
        return 0;

    /* 只使能A_FULL: FIFO达17样本触发一次INT, 约170ms唤醒一次任务
     * (不使能PPG_RDY, 避免100Hz高频唤醒) */
    (void)MAX30102_WriteReg(REG_INTR_ENABLE_1, 0x80);  /* A_FULL */
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

    MAX30102_IntInit();                                /* PC11/EXTI11 */

    return 1;
}

/* ---- 采样缓冲(静态, 避免任务栈溢出) ---- */
static uint32_t s_ir_buffer[BUFFER_LEN];   /* 红外数据 */
static uint32_t s_red_buffer[BUFFER_LEN];  /* 红光数据 */
static uint16_t s_sample_idx;              /* 当前写入位置 */

void MAX30102_SetNotifyTask(TaskHandle_t task)
{
    /* 须在MAX30102_Init(使能EXTI)之前注册, 避免ISR访问竞态 */
    s_notify_task = task;
}

uint32_t MAX30102_WaitSampleEvent(uint32_t timeout_ms)
{
    /* timeout_ms=0 表示永久阻塞等待INT事件(纯中断驱动, 不轮询) */
    TickType_t ticks = (timeout_ms == 0) ? portMAX_DELAY
                                         : pdMS_TO_TICKS(timeout_ms);
    return ulTaskNotifyTake(pdTRUE, ticks);
}

/* INT唤醒后调用: 读FIFO全部待读样本并累积,
 * 满BUFFER_LEN计算一次, 返回1=本次完成一轮计算, result已更新 */
uint8_t MAX30102_Process(max30102_result_t *result)
{
    uint8_t pending, i;
    uint8_t calc_done = 0;

    pending = MAX30102_PendingCount();
    if (pending > MAX30102_FIFO_DEPTH)
        pending = MAX30102_FIFO_DEPTH;

    if (pending > 0)
    {
        /* 读清中断状态: A_FULL的INT引脚保持低直到读该寄存器,
         * 读清后下个A_FULL事件才能产生新的下降沿 */
        (void)MAX30102_ReadReg(REG_INTR_STATUS_1);
        (void)MAX30102_ReadReg(REG_INTR_STATUS_2);
    }

    for (i = 0; i < pending; i++)
    {
        if (s_sample_idx >= BUFFER_LEN)
            break;  /* 缓冲满, 剩余样本留在FIFO等下轮 */

        if (MAX30102_ReadFifoSample(&s_red_buffer[s_sample_idx],
                                    &s_ir_buffer[s_sample_idx]) != 0)
            break;  /* 通信失败, 本轮放弃 */

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
