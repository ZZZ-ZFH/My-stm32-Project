/**
 * @file    hal_gpio.c
 * @brief   HAL层通用GPIO驱动实现(基于StdPeriph, 封装寄存器操作细节)
 */
#include "hal_gpio.h"

/* 计算引脚号(PinSource): GPIO_Pin_x -> x */
static uint8_t gpio_pin_source(uint16_t pin)
{
    uint8_t source = 0;
    uint16_t temp = pin;

    while ((temp >>= 1) != 0)
    {
        source++;
    }
    return source;
}

void HAL_GPIO_Init(const HAL_GPIO_Config_t *cfg)
{
    GPIO_InitTypeDef gpio_struct;

    if (cfg == NULL)
    {
        return;
    }

    /* 1. 使能端口时钟 */
    RCC_AHB1PeriphClockCmd(cfg->clk, ENABLE);

    /* 2. 配置GPIO(uint32_t配置值转换为标准库枚举类型) */
    gpio_struct.GPIO_Pin   = cfg->pin;
    gpio_struct.GPIO_Mode  = (GPIOMode_TypeDef)cfg->mode;
    gpio_struct.GPIO_OType = (GPIOOType_TypeDef)cfg->otype;
    gpio_struct.GPIO_Speed = (GPIOSpeed_TypeDef)cfg->speed;
    gpio_struct.GPIO_PuPd  = (GPIOPuPd_TypeDef)cfg->pull;
    GPIO_Init(cfg->port, &gpio_struct);

    /* 3. 复用模式下配置引脚复用功能 */
    if (cfg->mode == GPIO_Mode_AF)
    {
        GPIO_PinAFConfig(cfg->port, gpio_pin_source(cfg->pin), cfg->af);
    }
}

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, uint8_t level)
{
    if (level != 0)
    {
        GPIO_SetBits(port, pin);
    }
    else
    {
        GPIO_ResetBits(port, pin);
    }
}

uint8_t HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
    return (uint8_t)GPIO_ReadInputDataBit(port, pin);
}

void HAL_GPIO_TogglePin(GPIO_TypeDef *port, uint16_t pin)
{
    GPIO_ToggleBits(port, pin);
}
