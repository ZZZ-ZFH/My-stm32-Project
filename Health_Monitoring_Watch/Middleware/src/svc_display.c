/**
 * @file    svc_display.c
 * @brief   Middleware层显示服务实现: 封装ST7789背光控制
 */
#include "svc_display.h"
#include "st7789.h"

void SVC_DISPLAY_SetBacklight(uint8_t duty)
{
    ST7789_Set_Backlight(duty);
}
