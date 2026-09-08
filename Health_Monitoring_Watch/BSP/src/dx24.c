/**
 * @file    dx24.c
 * @brief   DX24蓝牙模块板级驱动(USART6): 引脚/外设/中断等板级配置集中于
 *          hardware_config.h, UART初始化/收发/中断统一经由 HAL 层(hal_uart/hal_gpio)
 *          接收: RXNE逐字节入缓冲(HAL回调), IDLE一帧结束(HAL回调)并通知任务
 *          发送: 查询TXE阻塞发送
 */
#include "dx24.h"
#include "hal_uart.h"
#include "hal_gpio.h"
#include "hardware_config.h"
#include <string.h>

/*
蓝牙模块接线(引脚配置见 hardware_config.h):
U6_TX  PC6  --> 蓝牙RXD
U6_RX  PC7  <-- 蓝牙TXD
STATE  PB6  --> 连接状态(手机连接后为高电平)
*/

/* USART6 板级配置描述符: 波特率由 hardware_config.h 提供 */
static const HAL_UART_Config_t s_bt_uart_cfg = {
    .instance    = HW_BT_UART_INSTANCE,
    .apb_bus     = HW_BT_UART_APB_BUS,
    .clk         = HW_BT_UART_CLK,
    .baudrate    = HW_BT_UART_BAUDRATE,
    .irq_channel = HW_BT_UART_IRQ_CHANNEL,
    .irq_preempt = HW_BT_UART_IRQ_PREEMPT,
    .irq_sub     = HW_BT_UART_IRQ_SUB,
    .tx = {
        .port  = HW_BT_TX_PORT,
        .pin   = HW_BT_TX_PIN,
        .clk   = HW_BT_TX_GPIO_CLK,
        .mode  = GPIO_Mode_AF,
        .otype = GPIO_OType_PP,
        .speed = GPIO_Medium_Speed,
        .pull  = GPIO_PuPd_UP,
        .af    = HW_BT_UART_GPIO_AF,
    },
    .rx = {
        .port  = HW_BT_RX_PORT,
        .pin   = HW_BT_RX_PIN,
        .clk   = HW_BT_RX_GPIO_CLK,
        .mode  = GPIO_Mode_AF,
        .otype = GPIO_OType_PP,
        .speed = GPIO_Medium_Speed,
        .pull  = GPIO_PuPd_UP,
        .af    = HW_BT_UART_GPIO_AF,
    },
};

/* STATE脚配置: 输入下拉(悬空时为断开态, 防误报已连接) */
static const HAL_GPIO_Config_t s_state_gpio = {
    .port  = DX24_STATE_PORT,
    .pin   = DX24_STATE_PIN,
    .clk   = DX24_STATE_GPIO_CLK,
    .mode  = GPIO_Mode_IN,
    .otype = GPIO_OType_PP,
    .speed = GPIO_Medium_Speed,
    .pull  = GPIO_PuPd_DOWN,
    .af    = 0,
};

/* 接收缓冲(中断写入, 任务读取后清零) */
uint8_t dx_rx_buf[DX24_RX_BUF_LEN] = {0};
volatile uint16_t dx_rx_len  = 0;   /* 本帧长度, IDLE中断时更新 */
volatile uint8_t dx_rx_flag = 0;   /* 1=收到一帧完整数据 */

/* 当前帧写入位置(ISR内部使用, 末字节保留给'\0'终止符) */
static uint16_t s_rx_widx = 0;

/* 接收事件通知任务句柄, 由DX24_SetNotifyTask在Init前注册 */
static TaskHandle_t s_notify_task = NULL;

/* RXNE回调: 单字节入帧缓冲(溢出保护: 预留末字节给'\0') */
static void bt_rx_byte_cb(uint8_t data)
{
    if (s_rx_widx < DX24_RX_BUF_LEN - 1)
    {
        dx_rx_buf[s_rx_widx++] = data;
    }
}

/* IDLE回调: 一帧结束, 置标志并通知任务(中断上下文, FromISR安全) */
static void bt_rx_idle_cb(void)
{
    BaseType_t higher_woken = pdFALSE;

    if (s_rx_widx > 0)
    {
        dx_rx_len  = s_rx_widx;   /* 缓冲区初始化已为全0, 天然带'\0' */
        dx_rx_flag = 1;
        s_rx_widx  = 0;

        /* 通知任务处理本帧数据 */
        if (s_notify_task != NULL)
        {
            vTaskNotifyGiveFromISR(s_notify_task, &higher_woken);
            portYIELD_FROM_ISR(higher_woken);
        }
    }
}

void DX24_Init(void)
{
    /* STATE脚: 先于串口初始化, 保证连接状态读数有效 */
    HAL_GPIO_Init(&s_state_gpio);

    /* 串口初始化(时钟+引脚+外设+NVIC+中断使能) */
    HAL_UART_Init(&s_bt_uart_cfg);

    /* 注册接收回调: RXNE字节入缓冲, IDLE帧结束通知任务 */
    HAL_UART_SetRxCallback(HW_BT_UART_INSTANCE,
                           bt_rx_byte_cb, bt_rx_idle_cb);
}

void DX24_SetNotifyTask(TaskHandle_t task)
{
    /* 须在DX24_Init(使能中断)之前注册, 避免ISR访问竞态 */
    s_notify_task = task;
}

/* 读STATE脚: 高电平=手机已连接蓝牙 */
uint8_t DX24_IsConnected(void)
{
    return (HAL_GPIO_ReadPin(DX24_STATE_PORT, DX24_STATE_PIN) == 1) ? 1 : 0;
}

/* 取一帧数据: 关中断拷贝整帧并清空接收状态(为新帧腾出缓冲),
 * 调用者处理期间新帧到达只覆盖共享缓冲, 不影响已拷贝数据 */
uint8_t DX24_GetFrame(uint8_t *buf, uint16_t *len)
{
    if ((dx_rx_flag == 0) || (buf == 0) || (len == 0))
    {
        return 0;
    }

    __disable_irq();
    *len = dx_rx_len;
    memcpy(buf, (const void *)dx_rx_buf, dx_rx_len);
    dx_rx_len  = 0;
    dx_rx_flag = 0;
    memset((void *)dx_rx_buf, 0, sizeof(dx_rx_buf));
    __enable_irq();

    return 1;
}

/* USART6中断向量函数: 转调HAL层通用中断处理 */
void USART6_IRQHandler(void)
{
    HAL_UART_IRQHandler(HW_BT_UART_INSTANCE);
}

void DX24_SendStr(uint8_t *str, uint16_t len)
{
    HAL_UART_SendBuffer(HW_BT_UART_INSTANCE, str, len);
}
