/**
 * @file    bsp_led.h
 * @brief   BSP层板级LED适配: 基于HAL_LED通用框架实例化本板LED设备
 *
 * 板级硬件(差异化配置见bsp_led.c设备表):
 *   LED1: PF9  低电平点亮
 *   LED2: PF10 低电平点亮
 * 扩展说明: 新增LED仅需在bsp_led.c设备表中追加一项描述符,
 *           通用操作逻辑全部复用HAL层的单一实现
 */
#ifndef __BSP_LED_H
#define __BSP_LED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 板级LED编号 */
typedef enum {
    BSP_LED_1 = 0,      /* PF9 */
    BSP_LED_2,          /* PF10 */
    BSP_LED_NUM         /* LED总数(设备表长度) */
} BSP_LED_Id_t;

/* BSP层LED接口(供应用层调用) */
void BSP_LED_Init(void);                       /* 初始化板上全部LED(默认熄灭) */
void BSP_LED_On(BSP_LED_Id_t id);              /* 点亮指定LED */
void BSP_LED_Off(BSP_LED_Id_t id);             /* 熄灭指定LED */
void BSP_LED_Toggle(BSP_LED_Id_t id);          /* 翻转指定LED */
void BSP_LED_Set(BSP_LED_Id_t id, uint8_t on); /* 置指定LED状态: 1=亮, 0=灭 */

#ifdef __cplusplus
}
#endif

#endif /* __BSP_LED_H */
