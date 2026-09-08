/**
 * @file    bsp_uart.c
 * @brief   BSP层板级调试串口适配实现
 *          引脚/波特率/中断优先级等板级参数在此集中配置,
 *          收发逻辑复用HAL_UART单一通用实现
 */
#include "bsp_uart.h"
#include "hal_uart.h"
#include "hardware_config.h"
#include <string.h>

/* 板级串口配置描述符: 引脚/外设/中断等板级配置
 * 集中于 hardware_config.h, 此表仅做组装(Init入参覆盖波特率) */
static const HAL_UART_Config_t s_uart1_cfg = {
    .instance    = HW_DBG_UART_INSTANCE,
    .apb_bus     = HW_DBG_UART_APB_BUS,
    .clk         = HW_DBG_UART_CLK,
    .baudrate    = HW_DBG_UART_BAUDRATE,
    .irq_channel = HW_DBG_UART_IRQ_CHANNEL,
    .irq_preempt = HW_DBG_UART_IRQ_PREEMPT,
    .irq_sub     = HW_DBG_UART_IRQ_SUB,
    .tx = {
        .port  = HW_DBG_TX_PORT,
        .pin   = HW_DBG_TX_PIN,
        .clk   = HW_DBG_TX_GPIO_CLK,
        .mode  = GPIO_Mode_AF,
        .otype = GPIO_OType_PP,
        .speed = GPIO_Medium_Speed,
        .pull  = GPIO_PuPd_UP,
        .af    = HW_DBG_UART_GPIO_AF,
    },
    .rx = {
        .port  = HW_DBG_RX_PORT,
        .pin   = HW_DBG_RX_PIN,
        .clk   = HW_DBG_RX_GPIO_CLK,
        .mode  = GPIO_Mode_AF,
        .otype = GPIO_OType_PP,
        .speed = GPIO_Medium_Speed,
        .pull  = GPIO_PuPd_UP,
        .af    = HW_DBG_UART_GPIO_AF,
    },
};

/* 帧接收缓冲(中断写入, GetRxFrame读取) */
static uint8_t           s_rx_buf[BSP_UART_RX_BUF_SIZE];
static volatile uint16_t s_rx_index = 0;    /* 当前帧写入长度 */
static volatile uint8_t  s_rx_flag  = 0;    /* 1=收到一帧完整数据 */

/* RXNE回调: 单字节入帧缓冲(带边界保护) */
static void uart1_rx_byte_cb(uint8_t data)
{
    if (s_rx_index < BSP_UART_RX_BUF_SIZE)
    {
        s_rx_buf[s_rx_index++] = data;
    }
}

/* IDLE回调: 一帧结束, 置帧就绪标志 */
static void uart1_rx_idle_cb(void)
{
    if (s_rx_index > 0)
    {
        s_rx_flag = 1;
    }
}

void BSP_UART_Init(void)
{
    HAL_UART_Config_t cfg = s_uart1_cfg;

    cfg.baudrate = HW_DBG_UART_BAUDRATE;   /* 波特率板级配置见 hardware_config.h */
    HAL_UART_Init(&cfg);

    /* 注册接收回调后, 中断数据进入本模块帧缓冲 */
    HAL_UART_SetRxCallback(HW_DBG_UART_INSTANCE,
                           uart1_rx_byte_cb, uart1_rx_idle_cb);
}

void BSP_UART_SendByte(uint8_t data)
{
    HAL_UART_SendByte(HW_DBG_UART_INSTANCE, data);
}

void BSP_UART_SendBuffer(const uint8_t *buf, uint16_t len)
{
    HAL_UART_SendBuffer(HW_DBG_UART_INSTANCE, buf, len);
}

uint8_t BSP_UART_GetRxFrame(uint8_t *buf, uint16_t *len)
{
    if ((s_rx_flag == 0) || (buf == NULL) || (len == NULL))
    {
        return 0;
    }

    /* 拷贝整帧数据并清空接收状态(为新帧腾出缓冲) */
    __disable_irq();
    *len = s_rx_index;
    memcpy(buf, (const void *)s_rx_buf, s_rx_index);
    s_rx_index = 0;
    s_rx_flag  = 0;
    __enable_irq();

    return 1;
}

/* USART1中断向量函数: 转调HAL层通用中断处理 */
void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(HW_DBG_UART_INSTANCE);
}
