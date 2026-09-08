/**
 * @file    hal_dma.h
 * @brief   HAL层单流DMA驱动: 内存到外设单次传输 + 传输完成中断
 *          与具体板级硬件无关, 流/通道/外设地址由BSP层通过配置结构体注入
 * @note    当前仅支持一路已配置的流(单消费者场景: LCD刷屏DMA),
 *          后续增加DMA使用者时再扩展为多流表驱动
 */
#ifndef __HAL_DMA_H
#define __HAL_DMA_H

#include "stm32f4xx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 传输完成回调(ISR上下文, 不得阻塞, 可为NULL) */
typedef void (*HAL_DMA_TCCallback_t)(void);

/* DMA单流配置结构体(BSP层填充板级参数) */
typedef struct {
    DMA_Stream_TypeDef *stream;     /* DMA数据流: DMAx_StreamN */
    uint32_t            channel;    /* 通道选择: DMA_Channel_x */
    uint32_t            clk;        /* DMA控制器时钟: RCC_AHB1Periph_DMAx */
    uint32_t            periph_addr;/* 外设数据寄存器地址(如 &SPI1->DR) */
    uint32_t            psize;      /* 外设数据宽度: DMA_PeripheralDataSize_x */
    uint32_t            msize;      /* 内存数据宽度: DMA_MemoryDataSize_x */
    uint32_t            irq_channel;/* 中断通道: DMAx_StreamN_IRQn */
    uint8_t             irq_preempt;/* 抢占优先级(调用FromISR API须>=5) */
    uint8_t             irq_sub;    /* 子优先级 */
    HAL_DMA_TCCallback_t tc_cb;     /* 传输完成回调(ISR上下文) */
} HAL_DMA_Config_t;

/* 初始化DMA流(时钟+NVIC+寄存器配置, 不启动传输) */
void    HAL_DMA_Init(const HAL_DMA_Config_t *cfg);

/* 启动一次内存->外设传输: 0=上一次传输尚未完成(或参数非法), 1=已启动
 * @note len单位为数据宽度个数, 上限65535(DMA NDTR为16位) */
uint8_t HAL_DMA_TxStart(const void *mem, uint16_t len);

/* 传输是否进行中: 1=进行中 */
uint8_t HAL_DMA_TxBusy(void);

/* DMA流中断通用处理(清TC/TE标志并回调tc_cb), BSP中断向量转调 */
void    HAL_DMA_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* __HAL_DMA_H */
