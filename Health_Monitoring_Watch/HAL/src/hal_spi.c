/**
 * @file    hal_spi.c
 * @brief   HAL层通用SPI驱动实现(基于StdPeriph, 封装寄存器操作细节)
 */
#include "hal_spi.h"

void HAL_SPI_Init(const HAL_SPI_Config_t *cfg)
{
    SPI_InitTypeDef spi_struct;

    if (cfg == NULL)
    {
        return;
    }

    /* 1. 使能SPI外设时钟(APB1或APB2由宏值自动匹配, RCC时钟树重复使能无害) */
    RCC_APB2PeriphClockCmd(cfg->clk, ENABLE);
    if (cfg->instance == SPI2 || cfg->instance == SPI3)
    {
        RCC_APB1PeriphClockCmd(cfg->clk, ENABLE);
    }

    /* 2. 初始化SCK/MOSI引脚(内部按GPIO_Mode_AF配置复用) */
    HAL_GPIO_Init(&cfg->sck);
    HAL_GPIO_Init(&cfg->mosi);

    /* 3. SPI主机参数: 8位MSB/软件NSS/无CRC */
    spi_struct.SPI_Direction         = SPI_Direction_2Lines_FullDuplex;
    spi_struct.SPI_Mode              = SPI_Mode_Master;
    spi_struct.SPI_DataSize          = SPI_DataSize_8b;
    spi_struct.SPI_CPOL              = cfg->cpol;
    spi_struct.SPI_CPHA              = cfg->cpha;
    spi_struct.SPI_NSS               = SPI_NSS_Soft;
    spi_struct.SPI_BaudRatePrescaler = cfg->prescaler;
    spi_struct.SPI_FirstBit          = SPI_FirstBit_MSB;
    spi_struct.SPI_CRCPolynomial     = 7;
    SPI_Init(cfg->instance, &spi_struct);

    SPI_Cmd(cfg->instance, ENABLE);
}

void HAL_SPI_SendByte(SPI_TypeDef *spi, uint8_t data)
{
    while (SPI_I2S_GetFlagStatus(spi, SPI_I2S_FLAG_TXE) == RESET)
    {
        /* 等待发送缓冲区为空 */
    }
    SPI_I2S_SendData(spi, data);
    /* 等待移位完成(BSY清零): 保证返回后总线空闲,
     * 紧跟的DC/CS电平变化不会打断字节传输 */
    while (SPI_I2S_GetFlagStatus(spi, SPI_I2S_FLAG_BSY) == SET)
    {
    }
}

void HAL_SPI_SendBuffer(SPI_TypeDef *spi, const uint8_t *buf, uint32_t len)
{
    while (len--)
    {
        while (SPI_I2S_GetFlagStatus(spi, SPI_I2S_FLAG_TXE) == RESET)
        {
            /* 等待发送缓冲区为空 */
        }
        SPI_I2S_SendData(spi, *buf++);
    }
    while (SPI_I2S_GetFlagStatus(spi, SPI_I2S_FLAG_BSY) == SET)
    {
        /* 末尾等待移位完成 */
    }
}

void HAL_SPI_EnableTxDMA(SPI_TypeDef *spi, uint8_t enable)
{
    SPI_I2S_DMACmd(spi, SPI_I2S_DMAReq_Tx,
                   (enable != 0) ? ENABLE : DISABLE);
}
