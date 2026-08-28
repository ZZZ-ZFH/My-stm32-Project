/**
 * @file    icm20602.c
 * @brief   ICM20602 六轴传感器驱动(标准库版本), 由模块资料参考代码移植
 *          硬件: 软件I2C PB0=SCL PC13=SDA (开漏+上拉)
 * @note    串口测试输出 USART1 printf, 波特率115200
 */
#include "icm20602.h"
#include "delay.h"
#include <stdio.h>

/* ==================== 模拟I2C 位操作 ==================== */
#define SDA_H()     GPIO_SetBits(ICM20602_SDA_PORT, ICM20602_SDA_PIN)
#define SDA_L()     GPIO_ResetBits(ICM20602_SDA_PORT, ICM20602_SDA_PIN)
#define SCL_H()     GPIO_SetBits(ICM20602_SCL_PORT, ICM20602_SCL_PIN)
#define SCL_L()     GPIO_ResetBits(ICM20602_SCL_PORT, ICM20602_SCL_PIN)
#define SDA_READ()  GPIO_ReadInputDataBit(ICM20602_SDA_PORT, ICM20602_SDA_PIN)

/* I2C 时序延时(约300kHz) */
#define I2C_DELAY()   { volatile uint16_t t = 30; while (t--); }

/* 等待SCL实际变高: 开漏输出下读IDR即总线真实电平 */
static void ICM20602_I2C_SclWait(void)
{
    uint16_t t = 500;
    while (GPIO_ReadInputDataBit(ICM20602_SCL_PORT,
           ICM20602_SCL_PIN) == Bit_RESET && t--)
        ;
}

/* -------------------- GPIO 初始化 -------------------- */
static void ICM20602_I2C_GpioInit(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB | RCC_AHB1Periph_GPIOC, ENABLE);

    /* SDA / SCL: 开漏+上拉 (PC13内部上拉较弱, 建议外接4.7K上拉) */
    GPIO_InitStructure.GPIO_Pin   = ICM20602_SCL_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_OD;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_Init(ICM20602_SCL_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = ICM20602_SDA_PIN;
    GPIO_Init(ICM20602_SDA_PORT, &GPIO_InitStructure);

    SDA_H();
    SCL_H();
}

/* -------------------- I2C 起止/字节 -------------------- */
static void ICM20602_I2C_Start(void)
{
    SDA_H(); SCL_H(); ICM20602_I2C_SclWait(); I2C_DELAY();
    SDA_L(); I2C_DELAY();
    SCL_L(); I2C_DELAY();
}

static void ICM20602_I2C_Stop(void)
{
    SDA_L(); SCL_L(); I2C_DELAY();
    SCL_H(); ICM20602_I2C_SclWait(); I2C_DELAY();
    SDA_H(); I2C_DELAY();
}

/* 发送一字节, 返回1=无应答 */
static uint8_t ICM20602_I2C_SendByte(uint8_t dat)
{
    uint8_t ack = 0, i;

    for (i = 0; i < 8; i++)
    {
        if (dat & 0x80) SDA_H(); else SDA_L();
        dat <<= 1;
        I2C_DELAY();
        SCL_H(); ICM20602_I2C_SclWait(); I2C_DELAY();
        SCL_L(); I2C_DELAY();
    }
    /* 读取ACK */
    SDA_H(); I2C_DELAY();
    SCL_H(); ICM20602_I2C_SclWait(); I2C_DELAY();
    if (SDA_READ()) ack = 1;
    SCL_L(); I2C_DELAY();
    return ack;
}

/* 接收一字节, nack=1时回NACK(最后一字节) */
static uint8_t ICM20602_I2C_ReadByte(uint8_t nack)
{
    uint8_t dat = 0, i;

    SDA_H();
    for (i = 0; i < 8; i++)
    {
        dat <<= 1;
        SCL_H(); ICM20602_I2C_SclWait(); I2C_DELAY();
        if (SDA_READ()) dat |= 1;
        SCL_L(); I2C_DELAY();
    }
    if (nack) SDA_H(); else SDA_L();   /* nack=1 -> NACK */
    I2C_DELAY();
    SCL_H(); ICM20602_I2C_SclWait(); I2C_DELAY();
    SCL_L(); SDA_H(); I2C_DELAY();
    return dat;
}

/* -------------------- 寄存器读写 -------------------- */
/* 实际器件地址(初始化时自动探测0x68/0x69) */
static uint8_t icm20602_dev_addr = ICM20602_ADDR;

/* 探测地址是否有应答, 返回1=有ACK */
static uint8_t ICM20602_Probe(uint8_t addr)
{
    uint8_t nack;

    ICM20602_I2C_Start();
    nack = ICM20602_I2C_SendByte(addr << 1);
    ICM20602_I2C_Stop();
    return (nack == 0);
}

/* I2C总线扫描: 打印所有有应答的器件地址(0x08~0x77) */
void ICM20602_ScanBus(void)
{
    uint8_t addr, found = 0;

    printf("I2C bus scan:");
    for (addr = 0x08; addr < 0x78; addr++)
    {
        if (ICM20602_Probe(addr))
        {
            printf(" 0x%02X", addr);
            found++;
        }
    }
    printf("%s\r\n", found ? "" : " (no device)");
}

/* 写寄存器, 返回0=成功 */
static uint8_t ICM20602_WriteReg(uint8_t reg, uint8_t dat)
{
    ICM20602_I2C_Start();
    if (ICM20602_I2C_SendByte(icm20602_dev_addr << 1)) { ICM20602_I2C_Stop(); return 1; }
    if (ICM20602_I2C_SendByte(reg))                     { ICM20602_I2C_Stop(); return 1; }
    if (ICM20602_I2C_SendByte(dat))                     { ICM20602_I2C_Stop(); return 1; }
    ICM20602_I2C_Stop();
    return 0;
}

/* 读寄存器, 返回0=成功 */
static uint8_t ICM20602_ReadRegs(uint8_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t i;

    ICM20602_I2C_Start();
    if (ICM20602_I2C_SendByte(icm20602_dev_addr << 1)) { ICM20602_I2C_Stop(); return 1; }
    if (ICM20602_I2C_SendByte(reg))                     { ICM20602_I2C_Stop(); return 1; }

    ICM20602_I2C_Start();                                /* 重复起始 */
    if (ICM20602_I2C_SendByte((icm20602_dev_addr << 1) | 1)) { ICM20602_I2C_Stop(); return 1; }
    for (i = 0; i < len; i++)
        buf[i] = ICM20602_I2C_ReadByte(i == len - 1);
    ICM20602_I2C_Stop();
    return 0;
}

/* -------------------- 对外接口 -------------------- */
/**
 * @brief  初始化ICM20602
 * @retval 1=成功 0=失败(总线无器件)
 * @note    自动探测0x68/0x69地址; WHO_AM_I不符仅告警不阻断(兼容芯片ID可能不同)
 */
uint8_t ICM20602_Init(void)
{
    uint8_t id = 0;

    ICM20602_I2C_GpioInit();
    delay_ms(10);

    /* 自动探测器件地址: SAO接GND为0x68, 接VCC为0x69 */
    if (ICM20602_Probe(0x69))       icm20602_dev_addr = 0x69;
    else if (ICM20602_Probe(0x68))  icm20602_dev_addr = 0x68;
    else
    {
        printf("ICM20602 error: no ACK at 0x68/0x69\r\n");
        ICM20602_ScanBus();
        return 0;
    }

    /* 读取WHO_AM_I: 不符仅告警(部分兼容芯片ID不同), 由数据验证通信 */
    if (ICM20602_ReadRegs(ICM20602_WHO_AM_I, &id, 1))
    {
        printf("ICM20602 error: read WHO_AM_I failed\r\n");
        return 0;
    }
    if (id != 0x12)
        printf("ICM20602 warning: WHO_AM_I=0x%02X (expect 0x12), continue\r\n", id);

    /* 与参考代码一致: 仅写入0唤醒(默认睡眠模式), 不做软复位 */
    ICM20602_WriteReg(ICM20602_PWR_MGMT_1, 0x00);   /* 唤醒设备 */
    delay_ms(50);                                    /* 等待时钟稳定 */

    /* 陀螺仪零偏校准: 需保持模块静止(约2秒) */
    ICM20602_Calibrate();

    printf("ICM20602 init OK (addr=0x%02X, WHO_AM_I=0x%02X)\r\n",
           icm20602_dev_addr, id);
    return 1;
}

/* -------------------- 陀螺仪零偏校准 -------------------- */
static int16_t gyro_offset[3] = {0, 0, 0};          /* 陀螺仪零偏 */

/**
 * @brief  陀螺仪零偏校准: 采集200样本求平均, 期间需保持静止
 * @note   校准期间约2秒; 若校准时晃动模块, 零偏会偏大
 */
void ICM20602_Calibrate(void)
{
    icm20602_raw_t raw;
    int32_t sum[3] = {0, 0, 0};
    const uint16_t n = 200;
    uint16_t i;

    printf("ICM20602 gyro calibrating, keep still...\r\n");
    delay_ms(100);                                  /* 等待采样稳定 */

    for (i = 0; i < n; i++)
    {
        ICM20602_ReadRaw(&raw);
        sum[0] += raw.gx;
        sum[1] += raw.gy;
        sum[2] += raw.gz;
        delay_ms(10);                               /* 总计约2秒 */
    }

    gyro_offset[0] = sum[0] / n;
    gyro_offset[1] = sum[1] / n;
    gyro_offset[2] = sum[2] / n;

    printf("gyro offset: %d, %d, %d\r\n",
           (int)gyro_offset[0], (int)gyro_offset[1], (int)gyro_offset[2]);
}

/**
 * @brief  读取加速度计+陀螺仪原始数据(12字节连续读)
 * @note   陀螺仪已减去零偏; 加速度计保留原始值(含重力)
 */
void ICM20602_ReadRaw(icm20602_raw_t *raw)
{
    uint8_t buf[14];

    if (ICM20602_ReadRegs(ICM20602_ACCEL_XOUT_H, buf, 14))
        return;                                     /* 通信失败, 数据不变 */

    raw->ax = (int16_t)((buf[0]  << 8) | buf[1]);
    raw->ay = (int16_t)((buf[2]  << 8) | buf[3]);
    raw->az = (int16_t)((buf[4]  << 8) | buf[5]);
    /* buf[6..7] = 温度, 此处不用 */
    raw->gx = (int16_t)((buf[8]  << 8) | buf[9])   - gyro_offset[0];
    raw->gy = (int16_t)((buf[10] << 8) | buf[11]) - gyro_offset[1];
    raw->gz = (int16_t)((buf[12] << 8) | buf[13]) - gyro_offset[2];
}

/**
 * @brief  测试函数: 循环读取并经USART1打印原始数据
 * @note   死循环测试用, 正式代码勿调用; 也可只调ICM20602_ReadRaw自行打印
 */
void ICM20602_Test(void)
{
    icm20602_raw_t raw;

    if (!ICM20602_Init())
        return;

    printf("Accel(X,Y,Z) | Gyro(X,Y,Z)\r\n");

    while (1)
    {
        ICM20602_ReadRaw(&raw);
        printf("A: %d, %d, %d\r\n", raw.ax, raw.ay, raw.az);
        printf("G: %d, %d, %d\r\n", raw.gx, raw.gy, raw.gz);
        printf("-------------------\r\n");
        delay_ms(100);
    }
}
