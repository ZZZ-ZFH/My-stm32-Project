/**
 * @file    st7789.h
 * @brief   ST7789 LCD 板级驱动接口: 引脚/外设配置在 st7789.c 内经HAL层注入,
 *          本头文件不引入芯片头文件, 对上层只暴露纯C接口
 */
#ifndef __ST7789_H
#define __ST7789_H

#include <stdint.h>

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
