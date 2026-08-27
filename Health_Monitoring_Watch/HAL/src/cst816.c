/**
 * @file    cst816.c
 * @brief   CST816 电容触摸驱动(标准库版本), 由 BSP/cst816.c(HAL版)移植而来
 *          硬件: 软件I2C PB8=SCL PB9=SDA (开漏+上拉)
 * @note    delay_ms 在 FreeRTOS 下可安全使用
 */
#include "cst816.h"
#include "delay.h"

/* ==================== 模拟I2C 位操作 ==================== */
#define SDA_H()     GPIO_SetBits(TP_I2C_SDA_PORT, TP_I2C_SDA_PIN)
#define SDA_L()     GPIO_ResetBits(TP_I2C_SDA_PORT, TP_I2C_SDA_PIN)
#define SCL_H()     GPIO_SetBits(TP_I2C_SCL_PORT, TP_I2C_SCL_PIN)
#define SCL_L()     GPIO_ResetBits(TP_I2C_SCL_PORT, TP_I2C_SCL_PIN)
#define SDA_READ()  GPIO_ReadInputDataBit(TP_I2C_SDA_PORT, TP_I2C_SDA_PIN)

/* I2C 时序延时(约300kHz, 杜邦线环境下保证裕量) */
#define TP_I2C_DELAY()   { volatile uint16_t t = 30; while (t--); }

/* 等待SCL实际变高: 开漏输出下读IDR即总线真实电平, 防止从机时钟拉伸导致读错数据 */
static void TP_I2C_SclWait(void)
{
    uint16_t t = 500;
    while (GPIO_ReadInputDataBit(TP_I2C_SCL_PORT, TP_I2C_SCL_PIN) == Bit_RESET && t--)
        ;
}

/* -------------------- GPIO 初始化 -------------------- */
static void TP_I2C_GpioInit(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);

    /* SDA / SCL: 开漏+上拉 */
    GPIO_InitStructure.GPIO_Pin   = TP_I2C_SDA_PIN | TP_I2C_SCL_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_OD;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    SDA_H();
    SCL_H();
}

/* -------------------- I2C 起止/字节 -------------------- */
static void TP_I2C_Start(void)
{
    SDA_H(); SCL_H(); TP_I2C_SclWait(); TP_I2C_DELAY();
    SDA_L(); TP_I2C_DELAY();
    SCL_L(); TP_I2C_DELAY();
}

static void TP_I2C_Stop(void)
{
    SDA_L(); SCL_L(); TP_I2C_DELAY();
    SCL_H(); TP_I2C_SclWait(); TP_I2C_DELAY();
    SDA_H(); TP_I2C_DELAY();
}

/* 发送一字节, 返回ACK(0=有应答) */
static uint8_t TP_I2C_SendByte(uint8_t dat)
{
    uint8_t ack = 0, i;

    for (i = 0; i < 8; i++)
    {
        if (dat & 0x80) SDA_H(); else SDA_L();
        dat <<= 1;
        TP_I2C_DELAY();
        SCL_H(); TP_I2C_SclWait(); TP_I2C_DELAY();
        SCL_L(); TP_I2C_DELAY();
    }
    /* 读取ACK */
    SDA_H(); TP_I2C_DELAY();
    SCL_H(); TP_I2C_SclWait(); TP_I2C_DELAY();
    if (SDA_READ()) ack = 1;
    SCL_L(); TP_I2C_DELAY();
    return ack;
}

/* 接收一字节, ack=1时回NACK(最后一字节) */
static uint8_t TP_I2C_ReadByte(uint8_t ack)
{
    uint8_t dat = 0, i;

    SDA_H();
    for (i = 0; i < 8; i++)
    {
        dat <<= 1;
        SCL_H(); TP_I2C_SclWait(); TP_I2C_DELAY();
        if (SDA_READ()) dat |= 1;
        SCL_L(); TP_I2C_DELAY();
    }
    if (ack) SDA_H(); else SDA_L();   /* ack=1 -> NACK */
    TP_I2C_DELAY();
    SCL_H(); TP_I2C_SclWait(); TP_I2C_DELAY();
    SCL_L(); SDA_H(); TP_I2C_DELAY();
    return dat;
}

/* ==================== CST816 寄存器读写 ==================== */
static uint8_t CST816_ReadRegs(uint8_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t i;

    TP_I2C_Start();
    if (TP_I2C_SendByte(CST816_I2C_ADDR << 1)) { TP_I2C_Stop(); return 1; }  /* 写地址 */
    if (TP_I2C_SendByte(reg))                  { TP_I2C_Stop(); return 1; }  /* 寄存器 */
    TP_I2C_Start();                                                             /* 重复起始 */
    if (TP_I2C_SendByte((CST816_I2C_ADDR << 1) | 1)) { TP_I2C_Stop(); return 1; } /* 读地址 */
    for (i = 0; i < len; i++)
    {
        buf[i] = TP_I2C_ReadByte(i == (len - 1) ? 1 : 0);   /* 最后一字节NACK */
    }
    TP_I2C_Stop();
    return 0;
}

static void CST816_WriteReg(uint8_t reg, uint8_t dat)
{
    TP_I2C_Start();
    TP_I2C_SendByte(CST816_I2C_ADDR << 1);
    TP_I2C_SendByte(reg);
    TP_I2C_SendByte(dat);
    TP_I2C_Stop();
}

/* ==================== 对外 API ==================== */
uint8_t CST816_ReadChipID(void)
{
    uint8_t id = 0;
    /* 0xA7为芯片ID寄存器, CST816S通常0xB5(也可能0xB4/B6) */
    CST816_ReadRegs(CST816_CHIP_ID_REG, &id, 1);
    return id;
}

uint8_t CST816_Init(void)
{
    uint8_t id;

    TP_I2C_GpioInit();

    /* RST未接: 上电等待模块内部复位完成 */
    delay_ms(TP_POWERUP_DELAY_MS);

    id = CST816_ReadChipID();
    if (id == 0x00 || id == 0xFF)      /* 无应答 */
        return 0;

    /* 注意: 此类CST816模块固件不允许写配置寄存器, 写0xF9会导致后续读数全0xFF, 因此不做任何写入 */
    return 1;
}

/* ==================== 触摸校准(四角最小二乘拟合) ====================
 * 屏幕四角(240x300)对应的原始触摸读数:
 *   左上(0,0)->(17,35)  右上(239,0)->(226,67)
 *   左下(0,299)->(12,258) 右下(239,299)->(230,278)
 * 拟合结果: 触摸原点(14.5,43.9), X缩放1.118, Y缩放1.374, 旋转约7°
 */
#define CAL_XA   1.118f     /* sx = XA*tx + XB*ty + XC */
#define CAL_XB   0.0034f
#define CAL_XC  -16.6f
#define CAL_YA  -0.166f     /* sy = YA*tx + YB*ty + YC */
#define CAL_YB   1.322f     /* 上部修正: 绕下端旋转, 上部下移~12px */
#define CAL_YC  -35.7f

#define LCD_WIDTH   240
#define LCD_HEIGHT  300

uint8_t CST816_GetPoint(cst816_point_t *point)
{
    uint8_t buf[6];
    float sx, sy;

    point->pressed = 0;

    /* 一次读取 手势/手指数/XY 共6字节, 减少占用 */
    if (CST816_ReadRegs(CST816_GESTURE_ID, buf, 6) != 0)
        return 0;

    /* 过滤无效数据: 手指数只可能为0/1, 全0xFF读到的是总线垃圾 */
    if (buf[1] == 0 || buf[1] > 1)
        return 0;

    {
        uint16_t tx = (uint16_t)((buf[2] & 0x0F) << 8) | buf[3];
        uint16_t ty = (uint16_t)((buf[4] & 0x0F) << 8) | buf[5];

        /* 坐标超范围说明是无效帧(如x=y=4095), 丢弃 */
        if (tx > 320 || ty > 320)
            return 0;

        /* 仿射校准: 原始触摸坐标 -> 屏幕坐标 */
        sx = CAL_XA * tx + CAL_XB * ty + CAL_XC;
        sy = CAL_YA * tx + CAL_YB * ty + CAL_YC;
    }

    /* 截断到屏幕范围 */
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    if (sx > LCD_WIDTH  - 1) sx = LCD_WIDTH  - 1;
    if (sy > LCD_HEIGHT - 1) sy = LCD_HEIGHT - 1;

    point->x = (uint16_t)sx;
    point->y = (uint16_t)sy;
    point->pressed = 1;
    return 1;
}

void CST816_Sleep(void)  { CST816_WriteReg(CST816_SLEEP_REG, 0x03); }
void CST816_Wakeup(void) /* 无RST线: CST816检测到I2C通信自动唤醒 */
{
    volatile uint8_t dummy = CST816_ReadChipID();
    (void)dummy;
    delay_ms(10);
}
