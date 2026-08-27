/**
 * @file    st7789.c
 * @brief   ST7789 LCD 底层驱动(标准库版本), 由 BSP/lcd_init.c(HAL版)移植而来
 *          硬件: SPI1(PB3-SCK, PB5-MOSI) + TIM2_CH3背光PWM(PA2) + RES(PB11)/CS(PA3)/DC(PB10)
 * @note    delay_ms 在 FreeRTOS 下可安全使用(见 HARDWARE/DELAY/delay.c)
 */
#include "st7789.h"
#include "delay.h"

/* 行缓冲, 提高整屏填充速度 */
static uint8_t s_line_buf[LCD_W * 2];

/* -------------------- SPI1 初始化 -------------------- */
static void ST7789_SPI_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    SPI_InitTypeDef   SPI_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);

    /* PB3-SCK PB5-MOSI: 复用推挽 */
    GPIO_InitStructure.GPIO_Pin   = LCD_SCL_PIN | LCD_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    GPIO_PinAFConfig(GPIOB, GPIO_PinSource3, GPIO_AF_SPI1);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource5, GPIO_AF_SPI1);

    /* SPI模式3: CPOL=1 CPHA=2Edge, 84M/2=42MHz */
    SPI_InitStructure.SPI_Direction         = SPI_Direction_2Lines_FullDuplex;
    SPI_InitStructure.SPI_Mode              = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize          = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL              = SPI_CPOL_High;
    SPI_InitStructure.SPI_CPHA              = SPI_CPHA_2Edge;
    SPI_InitStructure.SPI_NSS               = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_2;  /* 84M/2=42MHz, 保证长线/杜邦线下的信号完整性 */
    SPI_InitStructure.SPI_FirstBit          = SPI_FirstBit_MSB;
    SPI_InitStructure.SPI_CRCPolynomial     = 7;
    SPI_Init(SPI1, &SPI_InitStructure);

    SPI_Cmd(SPI1, ENABLE);
}

/* -------------------- 控制引脚初始化 -------------------- */
static void ST7789_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA | RCC_AHB1Periph_GPIOB, ENABLE);

    /* RES(PB11) CS(PA3) DC(PB10): 推挽输出 */
    GPIO_InitStructure.GPIO_Pin   = LCD_RES_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(LCD_RES_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = LCD_CS_PIN;
    GPIO_Init(LCD_CS_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = LCD_DC_PIN;
    GPIO_Init(LCD_DC_PORT, &GPIO_InitStructure);

    LCD_RES_Set();
    LCD_CS_Set();
    LCD_DC_Set();
}

/* -------------------- 背光 PWM 初始化: TIM2_CH3(PA2) -------------------- */
static void ST7789_Backlight_Init(void)
{
    GPIO_InitTypeDef        GPIO_InitStructure;
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    TIM_OCInitTypeDef       TIM_OCInitStructure;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = LCD_BLK_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(LCD_BLK_PORT, &GPIO_InitStructure);

    GPIO_PinAFConfig(GPIOA, GPIO_PinSource2, GPIO_AF_TIM2);

    /* 84MHz/84 = 1MHz 计数频率, 1MHz/100 = 10kHz PWM */
    TIM_TimeBaseStructure.TIM_Prescaler     = 84 - 1;
    TIM_TimeBaseStructure.TIM_Period        = 100 - 1;
    TIM_TimeBaseStructure.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);

    TIM_OCInitStructure.TIM_OCMode      = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse       = 100;             /* 默认全亮 */
    TIM_OCInitStructure.TIM_OCPolarity  = TIM_OCPolarity_High;
    TIM_OC3Init(TIM2, &TIM_OCInitStructure);

    TIM_OC3PreloadConfig(TIM2, TIM_OCPreload_Enable);
    TIM_Cmd(TIM2, ENABLE);
}

/* -------------------- 底层写入 -------------------- */
static void ST7789_Wr_Byte(uint8_t dat)
{
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET);
    SPI_I2S_SendData(SPI1, dat);
    /* 必须等待移位完成(BSY清零)才能返回:
       否则紧跟的DC拉高会发生在字节发送过程中, 命令被屏幕误判为数据 */
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) == SET);
}

void ST7789_Wr_Buf(const uint8_t *buf, uint32_t len)
{
    while (len--)
    {
        while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET);
        SPI_I2S_SendData(SPI1, *buf++);
    }
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) == SET);
}

static void ST7789_Wr_Reg(uint8_t reg)       /* DC=0 写命令 */
{
    LCD_DC_Clr();
    ST7789_Wr_Byte(reg);
    LCD_DC_Set();
}

static void ST7789_Wr_Data16(uint16_t dat)   /* 高字节在前 */
{
    uint8_t buf[2];
    buf[0] = (dat >> 8) & 0xFF;
    buf[1] = dat & 0xFF;
    ST7789_Wr_Buf(buf, 2);
}

/* -------------------- 功能 API -------------------- */
/* 240x300面板位于ST7789的320行GRAM中, 垂直偏移量(实测不准, 暂不启用) */
#define LCD_Y_OFFSET  0
#define LCD_X_OFFSET  0
void ST7789_Address_Set(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
#if USE_HORIZONTAL == 0 || USE_HORIZONTAL == 1
    y1 += LCD_Y_OFFSET;                     /* 竖屏: 垂直偏移 */
    y2 += LCD_Y_OFFSET;
#else
    x1 += LCD_X_OFFSET;                     /* 横屏: 偏移落在X轴 */
    x2 += LCD_X_OFFSET;
#endif
    ST7789_Wr_Reg(0x2A);                     /* 列地址 */
    ST7789_Wr_Data16(x1);
    ST7789_Wr_Data16(x2);
    ST7789_Wr_Reg(0x2B);                     /* 行地址 */
    ST7789_Wr_Data16(y1);
    ST7789_Wr_Data16(y2);
    ST7789_Wr_Reg(0x2C);                     /* 开始写显存 */
}

void ST7789_Fill(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    uint32_t w = (uint32_t)(x2 - x1 + 1);
    uint32_t h = (uint32_t)(y2 - y1 + 1);
    uint32_t i;

    for (i = 0; i < w; i++)                  /* 预生成一行数据(大端) */
    {
        s_line_buf[2 * i]     = (color >> 8) & 0xFF;
        s_line_buf[2 * i + 1] = color & 0xFF;
    }

    ST7789_Address_Set(x1, y1, x2, y2);
    for (i = 0; i < h; i++)
        ST7789_Wr_Buf(s_line_buf, w * 2);
}

void ST7789_Set_Backlight(uint8_t duty)
{
    if (duty > 100) duty = 100;
    TIM_SetCompare3(TIM2, (uint32_t)duty);   /* ARR=99, 比较值0~100 */
}

void ST7789_On(void)  { ST7789_Wr_Reg(0x29); }   /* Display ON */
void ST7789_Off(void) { ST7789_Wr_Reg(0x28); }   /* Display OFF */

/* -------------------- 屏幕初始化 -------------------- */
void ST7789_Init(void)
{
    ST7789_GPIO_Init();
    ST7789_SPI_Init();
    ST7789_Backlight_Init();

    LCD_CS_Clr();
    LCD_RES_Clr();
    delay_ms(100);
    LCD_RES_Set();
    delay_ms(100);

    ST7789_Wr_Reg(0x11);                     /* Sleep Out */
    delay_ms(120);

    ST7789_Wr_Reg(0x36);                     /* MADCTL 扫描方向 */
    if      (USE_HORIZONTAL == 0) ST7789_Wr_Byte(0x00);
    else if (USE_HORIZONTAL == 1) ST7789_Wr_Byte(0xC0);
    else if (USE_HORIZONTAL == 2) ST7789_Wr_Byte(0x70);
    else                          ST7789_Wr_Byte(0xA0);

    ST7789_Wr_Reg(0x3A);                     /* 16bit RGB565 */
    ST7789_Wr_Byte(0x05);

    ST7789_Wr_Reg(0xB2);                     /* Porch Setting */
    ST7789_Wr_Byte(0x0C); ST7789_Wr_Byte(0x0C); ST7789_Wr_Byte(0x00);
    ST7789_Wr_Byte(0x33); ST7789_Wr_Byte(0x33);

    ST7789_Wr_Reg(0xB7);                     /* Gate Control */
    ST7789_Wr_Byte(0x35);

    ST7789_Wr_Reg(0xBB);                     /* VCOM */
    ST7789_Wr_Byte(0x19);

    ST7789_Wr_Reg(0xC0);                     /* LCM Control */
    ST7789_Wr_Byte(0x2C);

    ST7789_Wr_Reg(0xC2);                     /* VDV/VRH Enable */
    ST7789_Wr_Byte(0x01);

    ST7789_Wr_Reg(0xC3);                     /* VRH Set */
    ST7789_Wr_Byte(0x12);

    ST7789_Wr_Reg(0xC4);                     /* VDV Set */
    ST7789_Wr_Byte(0x20);

    ST7789_Wr_Reg(0xC6);                     /* Frame Rate */
    ST7789_Wr_Byte(0x0F);

    ST7789_Wr_Reg(0xD0);                     /* Power Control */
    ST7789_Wr_Byte(0xA4); ST7789_Wr_Byte(0xA1);

    ST7789_Wr_Reg(0xE0);                     /* PV Gamma */
    ST7789_Wr_Byte(0xD0); ST7789_Wr_Byte(0x04); ST7789_Wr_Byte(0x0D);
    ST7789_Wr_Byte(0x11); ST7789_Wr_Byte(0x13); ST7789_Wr_Byte(0x2B);
    ST7789_Wr_Byte(0x3F); ST7789_Wr_Byte(0x54); ST7789_Wr_Byte(0x4C);
    ST7789_Wr_Byte(0x18); ST7789_Wr_Byte(0x0D); ST7789_Wr_Byte(0x0B);
    ST7789_Wr_Byte(0x1F); ST7789_Wr_Byte(0x23);

    ST7789_Wr_Reg(0xE1);                     /* NV Gamma */
    ST7789_Wr_Byte(0xD0); ST7789_Wr_Byte(0x04); ST7789_Wr_Byte(0x0C);
    ST7789_Wr_Byte(0x11); ST7789_Wr_Byte(0x13); ST7789_Wr_Byte(0x2C);
    ST7789_Wr_Byte(0x3F); ST7789_Wr_Byte(0x44); ST7789_Wr_Byte(0x51);
    ST7789_Wr_Byte(0x2F); ST7789_Wr_Byte(0x1F); ST7789_Wr_Byte(0x1F);
    ST7789_Wr_Byte(0x20); ST7789_Wr_Byte(0x23);

    ST7789_Wr_Reg(0x21);                     /* 反色(屏为IPS) */
    ST7789_Wr_Reg(0x29);                     /* Display ON */

    ST7789_Fill(0, 0, LCD_W - 1, LCD_H - 1, 0x0000);  /* 清屏为黑色 */
}

/* -------------------- 点屏测试: 八色彩条 -------------------- */
void LCD_Test(void)
{
    const uint16_t colors[8] = {0xFFFF, 0x0000, 0xF800, 0x07E0,   /* 白黑红绿 */
                                0x001F, 0x07FF, 0xF81F, 0xFFE0};  /* 蓝青品黄 */
    uint8_t  i;
    uint16_t h = LCD_H / 8;

    for (i = 0; i < 8; i++)
        ST7789_Fill(0, i * h, LCD_W - 1, (i + 1) * h - 1, colors[i]);

    /* 余下不足一行的部分用灰色补齐 */
    if (8 * h < LCD_H)
        ST7789_Fill(0, 8 * h, LCD_W - 1, LCD_H - 1, 0x8410);
}
