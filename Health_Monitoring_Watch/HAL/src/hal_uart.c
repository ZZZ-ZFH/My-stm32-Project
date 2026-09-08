/**
 * @file    hal_uart.c
 * @brief   HAL层通用UART驱动实现(多实例回调分发框架)
 */
#include "hal_uart.h"

/* 回调注册表容量(足够覆盖本工程使用的串口实例) */
#define HAL_UART_MAX_INSTANCE   4

/* 实例回调注册表项 */
typedef struct {
    USART_TypeDef          *instance;   /* 串口实例 */
    HAL_UART_RxByteCb_t     byte_cb;     /* RXNE字节回调 */
    HAL_UART_RxIdleCb_t     idle_cb;     /* IDLE帧结束回调 */
} HAL_UART_Reg_t;

static HAL_UART_Reg_t s_uart_reg[HAL_UART_MAX_INSTANCE];
static uint8_t s_uart_reg_num = 0;

/* 查找实例的回调注册项(未注册返回NULL) */
static HAL_UART_Reg_t *uart_reg_find(USART_TypeDef *uart)
{
    uint8_t i;

    for (i = 0; i < s_uart_reg_num; i++)
    {
        if (s_uart_reg[i].instance == uart)
        {
            return &s_uart_reg[i];
        }
    }
    return (HAL_UART_Reg_t *)0;
}

void HAL_UART_SetRxCallback(USART_TypeDef *uart,
                            HAL_UART_RxByteCb_t byte_cb,
                            HAL_UART_RxIdleCb_t idle_cb)
{
    HAL_UART_Reg_t *reg = uart_reg_find(uart);

    if (reg != (HAL_UART_Reg_t *)0)
    {
        reg->byte_cb = byte_cb;
        reg->idle_cb = idle_cb;
    }
}

void HAL_UART_Init(const HAL_UART_Config_t *cfg)
{
    USART_InitTypeDef usart_struct;
    NVIC_InitTypeDef  nvic_struct;

    if (cfg == NULL)
    {
        return;
    }

    /* 1. 使能串口时钟(挂APB1或APB2由BSP配置给出) */
    if (cfg->apb_bus == 1)
    {
        RCC_APB1PeriphClockCmd(cfg->clk, ENABLE);
    }
    else
    {
        RCC_APB2PeriphClockCmd(cfg->clk, ENABLE);
    }

    /* 2. 初始化TX/RX引脚(内部按GPIO_Mode_AF配置复用) */
    HAL_GPIO_Init(&cfg->tx);
    HAL_GPIO_Init(&cfg->rx);

    /* 3. 串口参数: 8N1无流控 */
    usart_struct.USART_BaudRate            = cfg->baudrate;
    usart_struct.USART_WordLength          = USART_WordLength_8b;
    usart_struct.USART_StopBits            = USART_StopBits_1;
    usart_struct.USART_Parity              = USART_Parity_No;
    usart_struct.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    usart_struct.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_Init(cfg->instance, &usart_struct);

    /* 4. NVIC接收中断配置 */
    nvic_struct.NVIC_IRQChannel                   = cfg->irq_channel;
    nvic_struct.NVIC_IRQChannelPreemptionPriority = cfg->irq_preempt;
    nvic_struct.NVIC_IRQChannelSubPriority        = cfg->irq_sub;
    nvic_struct.NVIC_IRQChannelCmd                 = ENABLE;
    NVIC_Init(&nvic_struct);

    /* 5. 登记回调注册表(空回调, 由BSP层随后注册) */
    if (uart_reg_find(cfg->instance) == (HAL_UART_Reg_t *)0
        && s_uart_reg_num < HAL_UART_MAX_INSTANCE)
    {
        s_uart_reg[s_uart_reg_num].instance = cfg->instance;
        s_uart_reg[s_uart_reg_num].byte_cb  = (HAL_UART_RxByteCb_t)0;
        s_uart_reg[s_uart_reg_num].idle_cb  = (HAL_UART_RxIdleCb_t)0;
        s_uart_reg_num++;
    }

    /* 6. 使能接收中断(RXNE字节 + IDLE帧结束)并启动串口 */
    USART_ITConfig(cfg->instance, USART_IT_RXNE, ENABLE);
    USART_ITConfig(cfg->instance, USART_IT_IDLE, ENABLE);
    USART_Cmd(cfg->instance, ENABLE);
}

void HAL_UART_SendByte(USART_TypeDef *uart, uint8_t data)
{
    USART_SendData(uart, data);
    while (USART_GetFlagStatus(uart, USART_FLAG_TXE) == RESET)
    {
        /* 等待发送缓冲区为空 */
    }
}

void HAL_UART_SendBuffer(USART_TypeDef *uart, const uint8_t *buf, uint16_t len)
{
    uint16_t i;

    for (i = 0; i < len; i++)
    {
        HAL_UART_SendByte(uart, buf[i]);
    }
}

void HAL_UART_IRQHandler(USART_TypeDef *uart)
{
    HAL_UART_Reg_t *reg = uart_reg_find(uart);

    /* 1. 接收中断: 单字节到达 */
    if (USART_GetITStatus(uart, USART_IT_RXNE) != RESET)
    {
        uint8_t data = (uint8_t)USART_ReceiveData(uart);

        if ((reg != (HAL_UART_Reg_t *)0) && (reg->byte_cb != (HAL_UART_RxByteCb_t)0))
        {
            reg->byte_cb(data);
        }
        USART_ClearITPendingBit(uart, USART_IT_RXNE);
    }

    /* 2. 空闲中断: 一帧结束(手册26.6.1: 先读SR再读DR清除IDLE标志) */
    if (USART_GetITStatus(uart, USART_IT_IDLE) != RESET)
    {
        (void)uart->SR;
        (void)uart->DR;

        if ((reg != (HAL_UART_Reg_t *)0) && (reg->idle_cb != (HAL_UART_RxIdleCb_t)0))
        {
            reg->idle_cb();
        }
    }
}
