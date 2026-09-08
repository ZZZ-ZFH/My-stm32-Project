/**
 * @file    hal_spi.h
 * @brief   HAL层通用SPI驱动: 配置结构体驱动的SPI主机初始化与发送
 *          与具体板级硬件无关, 引脚/外设参数由BSP层注入
 *
 * 发送语义:
 *   - HAL_SPI_SendByte  : 单字节完全完成(TXE->发送->BSY清零)返回,
 *                         适用于DC时序敏感的命令写入
 *   - HAL_SPI_SendBuffer: 逐字节流水发送, 末尾统一等BSY, 高吞吐
 */
#ifndef __HAL_SPI_H
#define __HAL_SPI_H

#include "stm32f4xx.h"
#include "hal_gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SPI通用配置结构体(BSP层填充板级参数) */
typedef struct {
    SPI_TypeDef       *instance;      /* SPI1~SPI6 */
    uint32_t           clk;           /* 外设时钟: RCC_APBxPeriph_SPIx */
    uint16_t           prescaler;     /* 波特率分频: SPI_BaudRatePrescaler_x */
    uint16_t           cpol;          /* 时钟极性: SPI_CPOL_High/Low */
    uint16_t           cpha;          /* 时钟相位: SPI_CPHA_1Edge/2Edge */
    HAL_GPIO_Config_t  sck;           /* SCK引脚配置(复用推挽) */
    HAL_GPIO_Config_t  mosi;          /* MOSI引脚配置(复用推挽) */
} HAL_SPI_Config_t;

/* HAL层通用SPI接口 */
void HAL_SPI_Init(const HAL_SPI_Config_t *cfg);                    /* 初始化SPI主机(时钟+引脚+外设) */
void HAL_SPI_SendByte(SPI_TypeDef *spi, uint8_t data);             /* 单字节完全完成(含BSY等待) */
void HAL_SPI_SendBuffer(SPI_TypeDef *spi,
                        const uint8_t *buf, uint32_t len);         /* 批量发送, 末尾等BSY */

#ifdef __cplusplus
}
#endif

#endif /* __HAL_SPI_H */
