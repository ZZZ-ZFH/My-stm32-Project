/**
 * @file    bsp_led.c
 * @brief   BSP层板级LED适配实现
 *          设备描述符表集中管理各LED的引脚与极性差异,
 *          操作逻辑全部复用HAL_LED单一通用实现
 */
#include "bsp_led.h"
#include "hal_led.h"
#include "hardware_config.h"

/* 板级LED设备描述符表: 端口/引脚/点亮极性等差异
 * 集中配置于 hardware_config.h, 此表仅做组装 */
static const HAL_LED_Device_t s_led_dev[BSP_LED_NUM] = {
    [BSP_LED_1] = {
        .gpio = {
            .port  = HW_LED1_PORT,
            .pin   = HW_LED1_PIN,
            .clk   = HW_LED1_GPIO_CLK,
            .mode  = GPIO_Mode_OUT,
            .otype = GPIO_OType_PP,
            .speed = GPIO_Speed_100MHz,
            .pull  = GPIO_PuPd_UP,
            .af    = 0,
        },
        .active_level = HW_LED1_ACTIVE_LEVEL,
    },
    [BSP_LED_2] = {
        .gpio = {
            .port  = HW_LED2_PORT,
            .pin   = HW_LED2_PIN,
            .clk   = HW_LED2_GPIO_CLK,
            .mode  = GPIO_Mode_OUT,
            .otype = GPIO_OType_PP,
            .speed = GPIO_Speed_100MHz,
            .pull  = GPIO_PuPd_UP,
            .af    = 0,
        },
        .active_level = HW_LED2_ACTIVE_LEVEL,
    },
};

void BSP_LED_Init(void)
{
    uint8_t id;

    for (id = 0; id < (uint8_t)BSP_LED_NUM; id++)
    {
        HAL_LED_Init(&s_led_dev[id]);
    }
}

void BSP_LED_On(BSP_LED_Id_t id)
{
    if (id < BSP_LED_NUM)
    {
        HAL_LED_On(&s_led_dev[id]);
    }
}

void BSP_LED_Off(BSP_LED_Id_t id)
{
    if (id < BSP_LED_NUM)
    {
        HAL_LED_Off(&s_led_dev[id]);
    }
}

void BSP_LED_Toggle(BSP_LED_Id_t id)
{
    if (id < BSP_LED_NUM)
    {
        HAL_LED_Toggle(&s_led_dev[id]);
    }
}

void BSP_LED_Set(BSP_LED_Id_t id, uint8_t on)
{
    if (id < BSP_LED_NUM)
    {
        HAL_LED_Set(&s_led_dev[id], on);
    }
}
