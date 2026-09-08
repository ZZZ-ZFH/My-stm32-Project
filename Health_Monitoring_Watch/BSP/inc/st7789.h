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
void ST7789_Init(void);                                    /* 屏幕初始化(SPI1+背光PWM+ST7789+刷屏DMA) */
void ST7789_Address_Set(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2); /* 设置显示区域 */
void ST7789_Wr_Buf(const uint8_t *buf, uint32_t len);      /* 写原始像素数据(大端RGB565), 阻塞发送 */

/* ---- DMA异步刷屏通路(供LVGL双缓冲流水刷新) ----
 * 使用约定(单消费者, 仅显示刷新任务调用):
 *   1. ST7789_Flush_Wait()   阻塞等待上一次DMA完成(信号量, 让出CPU)
 *   2. ST7789_Address_Set()  设置窗口(阻塞, 须在DMA空闲时)
 *   3. ST7789_Flush_Start()  启动本次DMA, 立即返回
 *   4. 传输完成由TC中断回调通知(注册于SetFlushDoneCB), 上层在回调中
 *      调用lv_disp_flush_ready; 回调运行在中断上下文, 不得阻塞 */
typedef void (*ST7789_FlushDone_cb_t)(void);               /* 刷屏完成回调(ISR上下文) */
void ST7789_SetFlushDoneCB(ST7789_FlushDone_cb_t cb);      /* 注册刷屏完成回调(可在任意时刻调用) */
uint8_t ST7789_Flush_Start(const uint8_t *buf, uint32_t len); /* 异步DMA写像素, 1=已启动, 0=忙或超长 */
void ST7789_Flush_Wait(void);                              /* 阻塞等待DMA完成(带超时自恢复) */

void ST7789_Fill(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color); /* 区域填充(阻塞, 仅初始化用) */
void ST7789_Set_Backlight(uint8_t duty);                   /* 背光亮度 0~100 */
void ST7789_On(void);                                      /* 开显示 */
void ST7789_Off(void);                                     /* 关显示 */
void LCD_Test(void);                                       /* 点屏测试: 画八色彩条 */

#endif
