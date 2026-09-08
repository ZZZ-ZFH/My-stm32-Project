/**
 * @file    st7789.c
 * @brief   ST7789 LCD 板级驱动: 引脚/外设参数集中于 hardware_config.h,
 *          GPIO/SPI/PWM 初始化与收发统一经由 HAL 层(hal_gpio/hal_spi/hal_tim)
 *          硬件: SPI1(PB3-SCK, PB5-MOSI) + TIM2_CH3背光PWM(PA2) + RES(PB11)/CS(PA3)/DC(PB10)
 * @note    HAL_Delay_Ms 在 FreeRTOS 下可安全使用(见 HAL/src/hal_delay.c)
 */
#include "st7789.h"
#include "hal_delay.h"
#include "hal_gpio.h"
#include "hal_spi.h"
#include "hal_tim.h"
#include "hardware_config.h"

/* ==================== IO 操作宏(驱动内部使用, 经HAL层) ==================== */
#define LCD_RES_Clr()   HAL_GPIO_WritePin(LCD_RES_PORT, LCD_RES_PIN, 0)
#define LCD_RES_Set()   HAL_GPIO_WritePin(LCD_RES_PORT, LCD_RES_PIN, 1)
#define LCD_CS_Clr()    HAL_GPIO_WritePin(LCD_CS_PORT, LCD_CS_PIN, 0)
#define LCD_CS_Set()    HAL_GPIO_WritePin(LCD_CS_PORT, LCD_CS_PIN, 1)
#define LCD_DC_Clr()    HAL_GPIO_WritePin(LCD_DC_PORT, LCD_DC_PIN, 0)
#define LCD_DC_Set()    HAL_GPIO_WritePin(LCD_DC_PORT, LCD_DC_PIN, 1)

/* 行缓冲, 提高整屏填充速度 */
static uint8_t s_line_buf[LCD_W * 2];

/* -------------------- SPI1 板级配置描述符 -------------------- */
static const HAL_SPI_Config_t s_spi1_cfg = {
    .instance  = LCD_SPI_INSTANCE,
    .clk       = LCD_SPI_CLK,
    .prescaler = LCD_SPI_PRESCALER,
    .cpol      = LCD_SPI_CPOL,
    .cpha      = LCD_SPI_CPHA,
    .sck = {
        .port  = LCD_SCL_PORT,
        .pin   = LCD_SCL_PIN,
        .clk   = LCD_SCL_GPIO_CLK,
        .mode  = GPIO_Mode_AF,
        .otype = GPIO_OType_PP,
        .speed = GPIO_Speed_100MHz,
        .pull  = GPIO_PuPd_UP,
        .af    = LCD_SPI_GPIO_AF,
    },
    .mosi = {
        .port  = LCD_SDA_PORT,
        .pin   = LCD_SDA_PIN,
        .clk   = LCD_SDA_GPIO_CLK,
        .mode  = GPIO_Mode_AF,
        .otype = GPIO_OType_PP,
        .speed = GPIO_Speed_100MHz,
        .pull  = GPIO_PuPd_UP,
        .af    = LCD_SPI_GPIO_AF,
    },
};

/* -------------------- 控制引脚配置描述符 -------------------- */
static const HAL_GPIO_Config_t s_res_gpio = {
    .port  = LCD_RES_PORT,
    .pin   = LCD_RES_PIN,
    .clk   = LCD_RES_GPIO_CLK,
    .mode  = GPIO_Mode_OUT,
    .otype = GPIO_OType_PP,
    .speed = GPIO_Speed_100MHz,
    .pull  = GPIO_PuPd_UP,
    .af    = 0,
};

static const HAL_GPIO_Config_t s_cs_gpio = {
    .port  = LCD_CS_PORT,
    .pin   = LCD_CS_PIN,
    .clk   = LCD_CS_GPIO_CLK,
    .mode  = GPIO_Mode_OUT,
    .otype = GPIO_OType_PP,
    .speed = GPIO_Speed_100MHz,
    .pull  = GPIO_PuPd_UP,
    .af    = 0,
};

static const HAL_GPIO_Config_t s_dc_gpio = {
    .port  = LCD_DC_PORT,
    .pin   = LCD_DC_PIN,
    .clk   = LCD_DC_GPIO_CLK,
    .mode  = GPIO_Mode_OUT,
    .otype = GPIO_OType_PP,
    .speed = GPIO_Speed_100MHz,
    .pull  = GPIO_PuPd_UP,
    .af    = 0,
};

/* -------------------- 背光 PWM 配置描述符 -------------------- */
static const HAL_TIM_PWMConfig_t s_bl_pwm_cfg = {
    .instance  = LCD_BL_PWM_INSTANCE,
    .clk       = LCD_BL_PWM_CLK,
    .prescaler = LCD_BL_PWM_PSC,
    .period    = LCD_BL_PWM_ARR,
    .channel   = LCD_BL_PWM_CHANNEL,
    .gpio = {
        .port  = LCD_BLK_PORT,
        .pin   = LCD_BLK_PIN,
        .clk   = LCD_BLK_GPIO_CLK,
        .mode  = GPIO_Mode_AF,
        .otype = GPIO_OType_PP,
        .speed = GPIO_Speed_100MHz,
        .pull  = GPIO_PuPd_UP,
        .af    = LCD_BL_PWM_GPIO_AF,
    },
    .pulse     = 100,               /* 默认全亮 */
};

/* -------------------- 初始化(经HAL层) -------------------- */
static void ST7789_SPI_Init(void)
{
    HAL_SPI_Init(&s_spi1_cfg);
}

static void ST7789_GPIO_Init(void)
{
    HAL_GPIO_Init(&s_res_gpio);
    HAL_GPIO_Init(&s_cs_gpio);
    HAL_GPIO_Init(&s_dc_gpio);

    LCD_RES_Set();
    LCD_CS_Set();
    LCD_DC_Set();
}

static void ST7789_Backlight_Init(void)
{
    HAL_TIM_PWMInit(&s_bl_pwm_cfg);
}

/* -------------------- 底层写入(经HAL层) -------------------- */
static void ST7789_Wr_Byte(uint8_t dat)
{
    /* 单字节完全完成(含BSY等待): 保证紧跟的DC电平变化不破坏命令时序 */
    HAL_SPI_SendByte(LCD_SPI_INSTANCE, dat);
}

void ST7789_Wr_Buf(const uint8_t *buf, uint32_t len)
{
    HAL_SPI_SendBuffer(LCD_SPI_INSTANCE, buf, len);
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
    HAL_TIM_PWMSetPulse(LCD_BL_PWM_INSTANCE, LCD_BL_PWM_CHANNEL,
                        (uint32_t)duty);       /* ARR=99, 比较值0~100 */
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
    HAL_Delay_Ms(100);
    LCD_RES_Set();
    HAL_Delay_Ms(100);

    ST7789_Wr_Reg(0x11);                     /* Sleep Out */
    HAL_Delay_Ms(120);

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
