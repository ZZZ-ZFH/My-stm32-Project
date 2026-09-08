/**
 * @file    svc_display.h
 * @brief   Middleware层显示服务: 背光亮度控制(亮/熄屏),
 *          屏幕初始化由LVGL显示移植层完成, 对APP只暴露亮度接口
 */
#ifndef __SVC_DISPLAY_H
#define __SVC_DISPLAY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 背光亮度: 0=熄屏, 1~100=渐亮(占空比) */
void SVC_DISPLAY_SetBacklight(uint8_t duty);

#ifdef __cplusplus
}
#endif

#endif /* __SVC_DISPLAY_H */
