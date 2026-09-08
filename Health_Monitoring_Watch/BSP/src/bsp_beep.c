/**
 * @file    bsp_beep.c
 * @brief   BSP层板级蜂鸣器适配实现
 *          引脚/极性等板级配置集中于 hardware_config.h
 */
#include "bsp_beep.h"
#include "hal_gpio.h"
#include "hardware_config.h"

/* 蜂鸣器引脚描述符: 板级差异化配置见 hardware_config.h */
static const HAL_GPIO_Config_t s_beep_gpio = {
    .port  = HW_BEEP_PORT,
    .pin   = HW_BEEP_PIN,
    .clk   = HW_BEEP_GPIO_CLK,
    .mode  = GPIO_Mode_OUT,
    .otype = GPIO_OType_PP,
    .speed = GPIO_Medium_Speed,
    .pull  = GPIO_PuPd_DOWN,
    .af    = 0,
};

void BSP_BEEP_Init(void)
{
    HAL_GPIO_Init(&s_beep_gpio);
    BSP_BEEP_Off();   /* 默认静音 */
}

void BSP_BEEP_On(void)
{
    HAL_GPIO_WritePin(HW_BEEP_PORT, HW_BEEP_PIN, HW_BEEP_ACTIVE_LEVEL);
}

void BSP_BEEP_Off(void)
{
    HAL_GPIO_WritePin(HW_BEEP_PORT, HW_BEEP_PIN,
                      (uint8_t)(HW_BEEP_ACTIVE_LEVEL ^ 1u));
}

void BSP_BEEP_Toggle(void)
{
    HAL_GPIO_TogglePin(HW_BEEP_PORT, HW_BEEP_PIN);
}
