/**
 * @file    hal_led.h
 * @brief   HAL层通用LED设备驱动(单一通用实现)
 *
 * 设计说明:
 *   - HAL层只维护这一份LED通用逻辑(初始化/点亮/熄灭/翻转),
 *     不含任何板级引脚信息
 *   - BSP层通过 HAL_LED_Device_t 设备描述符注入差异化的
 *     引脚配置与点亮极性(active_level), 复用本通用实现
 *   - 同一框架可适配任意数量/任意极性的LED设备
 *
 * 使用步骤(BSP层):
 *   1. 定义设备描述符表(端口/引脚/时钟/点亮电平)
 *   2. 调用 HAL_LED_Init 逐个初始化
 *   3. 通过 HAL_LED_On/Off/Toggle/Set 操作指定设备
 */
#ifndef __HAL_LED_H
#define __HAL_LED_H

#include "stm32f4xx.h"
#include "hal_gpio.h"
#include "stdio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* LED设备描述符: BSP层按板级硬件差异化填充 */
typedef struct {
    HAL_GPIO_Config_t gpio;         /* 引脚配置(端口/引脚/时钟/模式等) */
    uint8_t           active_level; /* 点亮电平: 0=低电平点亮, 1=高电平点亮 */
} HAL_LED_Device_t;

/* HAL层通用LED接口 */
void HAL_LED_Init(const HAL_LED_Device_t *dev);                       /* 初始化LED(默认熄灭) */
void HAL_LED_On(const HAL_LED_Device_t *dev);                         /* 点亮 */
void HAL_LED_Off(const HAL_LED_Device_t *dev);                        /* 熄灭 */
void HAL_LED_Toggle(const HAL_LED_Device_t *dev);                     /* 翻转 */
void HAL_LED_Set(const HAL_LED_Device_t *dev, uint8_t on);            /* 置状态: 1=亮, 0=灭 */

#ifdef __cplusplus
}
#endif

#endif /* __HAL_LED_H */
