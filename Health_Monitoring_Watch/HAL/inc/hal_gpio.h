/**
 * @file    hal_gpio.h
 * @brief   HAL层通用GPIO驱动: 配置结构体驱动的引脚初始化与读写
 *          与具体板级硬件无关, 引脚参数由BSP层通过配置结构体注入
 */
#ifndef __HAL_GPIO_H
#define __HAL_GPIO_H

#include "stm32f4xx.h"
#include "stdio.h"
#ifdef __cplusplus
extern "C" {
#endif

/* GPIO通用配置结构体(BSP层填充板级引脚参数) */
typedef struct {
    GPIO_TypeDef *port;     /* GPIO端口: GPIOA~GPIOI */
    uint16_t      pin;     /* 引脚: GPIO_Pin_0 ~ GPIO_Pin_15 */
    uint32_t      clk;     /* 端口时钟: RCC_AHB1Periph_GPIOx */
    uint32_t      mode;    /* 模式: GPIO_Mode_IN/OUT/AF/AN */
    uint32_t      otype;   /* 输出类型: GPIO_OType_PP/OD */
    uint32_t      speed;   /* 速度: GPIO_Low/Medium/High/Speed_100MHz */
    uint32_t      pull;    /* 上下拉: GPIO_PuPd_NOPULL/UP/DOWN */
    uint8_t       af;      /* 复用功能编号(GPIO_Mode_AF时有效): GPIO_AF_xxx */
} HAL_GPIO_Config_t;

/* HAL层接口函数 */
void    HAL_GPIO_Init(const HAL_GPIO_Config_t *cfg);              /* 引脚初始化(含时钟/复用配置) */
void    HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, uint8_t level);  /* 写电平: 0/1 */
uint8_t HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);       /* 读输入电平: 0/1 */
void    HAL_GPIO_TogglePin(GPIO_TypeDef *port, uint16_t pin);     /* 翻转输出电平 */

#ifdef __cplusplus
}
#endif

#endif /* __HAL_GPIO_H */
