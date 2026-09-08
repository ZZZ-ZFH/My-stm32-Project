/**
 * @file    hal_led.c
 * @brief   HAL层通用LED设备驱动实现(全项目唯一一份LED通用逻辑)
 */
#include "hal_led.h"

/* LED初始化为推挽输出, 初始状态熄灭 */
void HAL_LED_Init(const HAL_LED_Device_t *dev)
{
    if (dev == NULL)
    {
        return;
    }

    /* 输出模式下复用/上下拉参数无意义, 统一由BSP层gpio配置给出 */
    HAL_GPIO_Init(&dev->gpio);

    /* 上电默认熄灭(与点亮电平相反) */
    HAL_LED_Off(dev);
}

void HAL_LED_On(const HAL_LED_Device_t *dev)
{
    if (dev == NULL)
    {
        return;
    }
    HAL_GPIO_WritePin(dev->gpio.port, dev->gpio.pin, dev->active_level);
}

void HAL_LED_Off(const HAL_LED_Device_t *dev)
{
    if (dev == NULL)
    {
        return;
    }
    HAL_GPIO_WritePin(dev->gpio.port, dev->gpio.pin,
                     (uint8_t)(dev->active_level ^ 1u));
}

void HAL_LED_Toggle(const HAL_LED_Device_t *dev)
{
    if (dev == NULL)
    {
        return;
    }
    HAL_GPIO_TogglePin(dev->gpio.port, dev->gpio.pin);
}

void HAL_LED_Set(const HAL_LED_Device_t *dev, uint8_t on)
{
    if (on != 0)
    {
        HAL_LED_On(dev);
    }
    else
    {
        HAL_LED_Off(dev);
    }
}
