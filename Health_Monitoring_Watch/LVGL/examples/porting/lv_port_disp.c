/**
 * @file lv_port_disp.c
 * @brief LVGL v8 显示接口移植 (ST7789 240x300 + 硬件SPI1, 标准库)
 */

/*Copy this file as "lv_port_disp.c" and set this value to "1" to enable content*/
#if 1

/*********************
 *      INCLUDES
 *********************/
#include "lv_port_disp.h"
#include <stdbool.h>
#include "st7789.h"

/*********************
 *      DEFINES
 *********************/
#define MY_DISP_HOR_RES    LCD_W       /* 240 */
#define MY_DISP_VER_RES    LCD_H       /* 300 */

/* 双缓冲绘制行数: 每缓冲240*10*2=4.8KB, 两缓冲共9.6KB(ZI)
 * 双缓冲下LVGL渲染另一缓冲期间DMA正在传输当前缓冲, 渲染与传输流水并行 */
#define DISP_BUF_LINES     10

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void disp_init(void);
static void disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p);
static void flush_done_cb(void);     /* DMA传输完成回调(st7789 TC中断上下文) */

/**********************
 *  STATIC VARIABLES
 **********************/
/* 正在DMA传输的显示驱动句柄: flush时登记, TC中断回调据此调用flush_ready
 * (32位对齐指针, 读写原子; 单刷新任务单显示, 无并发) */
static lv_disp_drv_t *s_flushing_drv;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_port_disp_init(void)
{
    /*-------------------------
     * Initialize your display
     * -----------------------*/
    disp_init();

    /*-----------------------------
     * Create a buffer for drawing
     *----------------------------*/

    /**
     * LVGL requires a buffer where it internally draws the widgets.
     * Later this buffer will passed to your display driver's `flush_cb` to copy its content to your display.
     * The buffer has to be greater than 1 display row
     *
     * There are 3 buffering configurations:
     * 1. Create ONE buffer:
     *      LVGL will draw the display's content here and writes it to your display
     *
     * 2. Create TWO buffer:
     *      LVGL will draw the display's content to a buffer and writes it your display.
     *      You should use DMA to write the buffer's content to the display.
     *      It will enable LVGL to draw the next part of the screen to the other buffer while
     *      the data is being sent form the first buffer. It makes rendering and flushing parallel.
     *
     * 3. Double buffering
     *      Set 2 screens sized buffers and set disp_drv.full_refresh = 1.
     *      This way LVGL will always provide the whole rendered screen in `flush_cb`
     *      and you only need to change the frame buffer's address.
     */

    /* 双缓冲(方案2): LVGL渲染其中一块期间, 另一块由DMA送往屏幕,
     * 渲染与传输流水并行; flush_cb异步返回, 完成由TC中断回调flush_ready */
    static lv_disp_draw_buf_t draw_buf_dsc;
    static lv_color_t buf_1[MY_DISP_HOR_RES * DISP_BUF_LINES];
    static lv_color_t buf_2[MY_DISP_HOR_RES * DISP_BUF_LINES];
    lv_disp_draw_buf_init(&draw_buf_dsc, buf_1, buf_2,
                          MY_DISP_HOR_RES * DISP_BUF_LINES);
    /*-----------------------------------
     * Register the display in LVGL
     *----------------------------------*/

    static lv_disp_drv_t disp_drv;                         /*Descriptor of a display driver*/
    lv_disp_drv_init(&disp_drv);                    /*Basic initialization*/

    disp_drv.hor_res  = MY_DISP_HOR_RES;
    disp_drv.ver_res  = MY_DISP_VER_RES;
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &draw_buf_dsc;

    /* 登记DMA完成回调: TC中断中调用flush_ready, 释放LVGL对该缓冲的占用 */
    ST7789_SetFlushDoneCB(flush_done_cb);

    lv_disp_drv_register(&disp_drv);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/*Initialize your display and the required peripherals.*/
static void disp_init(void)
{
    ST7789_Init();              /* SPI1 + 背光PWM + ST7789 寄存器初始化 */
    ST7789_Set_Backlight(100);  /* 背光最亮 */
}

volatile bool disp_flush_enabled = true;

/* Enable updating the screen (the flushing process) when disp_flush() is called by LVGL
 */
void disp_enable_update(void)
{
    disp_flush_enabled = true;
}

/* Disable updating the screen (the flushing process) when disp_flush() is called by LVGL
 */
void disp_disable_update(void)
{
    disp_flush_enabled = false;
}

/*Flush the content of the internal buffer the specific area on the display
 *DMA异步刷新: 等待上次DMA完成->设窗口->启动本次DMA后立即返回,
 *'lv_disp_flush_ready()'在DMA传输完成中断(flush_done_cb)中调用 */
static void disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p)
{
    if(disp_flush_enabled)
    {
        uint32_t size = (uint32_t)(area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1);

        /* 等待上一次DMA传完(阻塞在信号量上, CPU让给其他任务);
         * 双缓冲下此刻LVGL刚渲染完本缓冲, 上缓冲可能仍在传输 */
        ST7789_Flush_Wait();

        ST7789_Address_Set((uint16_t)area->x1, (uint16_t)area->y1,
                           (uint16_t)area->x2, (uint16_t)area->y2);

#if LV_COLOR_16_SWAP == 0
        /* LVGL为小端RGB565, ST7789需要大端: 原地交换高低字节
         * (操作的是本缓冲, 与仍在传输的另一缓冲无冲突) */
        {
            uint16_t *p = (uint16_t *)color_p;
            uint32_t i;
            for (i = 0; i < size; i++)
                p[i] = (uint16_t)((p[i] >> 8) | (p[i] << 8));
        }
#endif

        /* 先登记驱动句柄再启动DMA: TC中断据此回调flush_ready */
        s_flushing_drv = disp_drv;
        if (ST7789_Flush_Start((const uint8_t *)color_p, size * 2) == 0)
        {
            /* 防御回退: DMA启动失败(超长/忙)退化为阻塞发送 */
            s_flushing_drv = NULL;
            ST7789_Wr_Buf((const uint8_t *)color_p, size * 2);
            lv_disp_flush_ready(disp_drv);
        }
    }
    else
    {
        /* 刷新被禁用: 仍须报完成, 否则LVGL刷新流程挂起 */
        lv_disp_flush_ready(disp_drv);
    }
}

/* DMA传输完成回调(st7789 TC中断上下文, 不得阻塞):
 * 通知LVGL该缓冲已送出, 可继续渲染 */
static void flush_done_cb(void)
{
    if (s_flushing_drv != NULL)
    {
        lv_disp_flush_ready(s_flushing_drv);
        s_flushing_drv = NULL;
    }
}

/*OPTIONAL: GPU INTERFACE*/

/*If your MCU has hardware accelerator (GPU) then you can use it to fill a memory with a color*/
//static void gpu_fill(lv_disp_drv_t * disp_drv, lv_color_t * dest_buf, lv_coord_t dest_width,
//                    const lv_area_t * fill_area, lv_color_t color)
//{
//    /*It's an example code which should be done by your GPU*/
//    int32_t x, y;
//    dest_buf += dest_width * fill_area->y1; /*Go to the first line*/
//
//    for(y = fill_area->y1; y <= fill_area->y2; y++) {
//        for(x = fill_area->x1; x <= fill_area->x2; x++) {
//            dest_buf[x] = color;
//        }
//        dest_buf+=dest_width;    /*Go to the next line*/
//    }
//}

#else /*Enable this file at the top*/

/*This dummy typedef exists purely to silence -Wpedantic.*/
typedef int keep_pedantic_happy;
#endif
