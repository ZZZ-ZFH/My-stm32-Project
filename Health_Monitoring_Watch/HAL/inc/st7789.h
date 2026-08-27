#ifndef __ST7789_H
#define __ST7789_H

#include "stm32f4xx.h"

/* ==================== 屏幕参数 ==================== */
/* 240x280 ST7789 竖屏(参考 BSP/lcd_init) */
#define USE_HORIZONTAL  0        /* 0/1竖屏 2/3横屏 */

#if USE_HORIZONTAL == 0 || USE_HORIZONTAL == 1
#define LCD_W  240
#define LCD_H  300
#else
#define LCD_W  300
#define LCD_H  240
#endif

/* ==================== SPI1 引脚 ==================== */
#define LCD_SCL_PORT    GPIOB
#define LCD_SCL_PIN     GPIO_Pin_3      /* PB3 -> SPI1_SCK  (AF5) */
#define LCD_SDA_PORT    GPIOB
#define LCD_SDA_PIN     GPIO_Pin_5      /* PB5 -> SPI1_MOSI (AF5) */

/* ==================== 控制引脚 ==================== */
#define LCD_RES_PORT    GPIOB
#define LCD_RES_PIN     GPIO_Pin_11     /* PB11 复位 */
#define LCD_CS_PORT     GPIOA
#define LCD_CS_PIN      GPIO_Pin_3      /* PA3  片选 */
#define LCD_DC_PORT     GPIOB
#define LCD_DC_PIN      GPIO_Pin_10     /* PB10 数据/命令 */

/* ==================== 背光: TIM2_CH3 -> PA2 ==================== */
#define LCD_BLK_PORT    GPIOA
#define LCD_BLK_PIN     GPIO_Pin_2

/* ==================== IO 操作宏 ==================== */
#define LCD_RES_Clr()   GPIO_ResetBits(LCD_RES_PORT, LCD_RES_PIN)
#define LCD_RES_Set()   GPIO_SetBits(LCD_RES_PORT, LCD_RES_PIN)
#define LCD_CS_Clr()    GPIO_ResetBits(LCD_CS_PORT, LCD_CS_PIN)
#define LCD_CS_Set()    GPIO_SetBits(LCD_CS_PORT, LCD_CS_PIN)
#define LCD_DC_Clr()    GPIO_ResetBits(LCD_DC_PORT, LCD_DC_PIN)
#define LCD_DC_Set()    GPIO_SetBits(LCD_DC_PORT, LCD_DC_PIN)

/* ==================== API ==================== */
void ST7789_Init(void);                                    /* 屏幕初始化(SPI1+背光PWM+ST7789) */
void ST7789_Address_Set(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2); /* 设置显示区域 */
void ST7789_Wr_Buf(const uint8_t *buf, uint32_t len);      /* 写原始像素数据(大端RGB565), 供LVGL刷新用 */
void ST7789_Fill(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color); /* 区域填充 */
void ST7789_Set_Backlight(uint8_t duty);                   /* 背光亮度 0~100 */
void ST7789_On(void);                                      /* 开显示 */
void ST7789_Off(void);                                     /* 关显示 */
void LCD_Test(void);                                       /* 点屏测试: 画八色彩条 */

#endif
