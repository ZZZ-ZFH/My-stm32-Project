/**
 * @file    hal_dma.c
 * @brief   HAL层单流DMA驱动实现(基于StdPeriph, 封装寄存器操作细节)
 *          换芯片时仅改本文件(接口不变)
 */
#include "hal_dma.h"
#include <stddef.h>

/* Init时保存的运行信息(Start/IRQHandler共用) */
static DMA_Stream_TypeDef   *s_stream;
static HAL_DMA_TCCallback_t  s_tc_cb;
static DMA_InitTypeDef       s_dma_tmpl;   /* Init时的流配置模板 */

/* 流号(0~7)对应的标志位(查表, StdPeriph各流宏值非线性编排) */
static const uint32_t s_flag_tc[8] = {
    DMA_IT_TCIF0, DMA_IT_TCIF1, DMA_IT_TCIF2, DMA_IT_TCIF3,
    DMA_IT_TCIF4, DMA_IT_TCIF5, DMA_IT_TCIF6, DMA_IT_TCIF7,
};
static const uint32_t s_flag_te[8] = {
    DMA_IT_TEIF0, DMA_IT_TEIF1, DMA_IT_TEIF2, DMA_IT_TEIF3,
    DMA_IT_TEIF4, DMA_IT_TEIF5, DMA_IT_TEIF6, DMA_IT_TEIF7,
};

/* 流指针 -> 流号(0~7), 非法流返回0xFF */
static uint8_t dma_stream_no(const DMA_Stream_TypeDef *stream)
{
    if (stream >= DMA1_Stream0 && stream <= DMA1_Stream7)
        return (uint8_t)(stream - DMA1_Stream0);
    if (stream >= DMA2_Stream0 && stream <= DMA2_Stream7)
        return (uint8_t)(stream - DMA2_Stream0);
    return 0xFF;
}

void HAL_DMA_Init(const HAL_DMA_Config_t *cfg)
{
    NVIC_InitTypeDef nvic_struct;
    uint8_t no;

    if (cfg == NULL || cfg->stream == NULL ||
        dma_stream_no(cfg->stream) == 0xFF)
    {
        return;
    }

    s_stream = cfg->stream;
    s_tc_cb  = cfg->tc_cb;

    /* 1.DMA控制器时钟 */
    RCC_AHB1PeriphClockCmd(cfg->clk, ENABLE);

    /* 2.流配置模板: 单次模式/外设地址固定/内存递增/直通模式(无FIFO) */
    s_dma_tmpl.DMA_Channel            = cfg->channel;
    s_dma_tmpl.DMA_PeripheralBaseAddr = cfg->periph_addr;
    s_dma_tmpl.DMA_Memory0BaseAddr    = 0;    /* 每次TxStart时设置 */
    s_dma_tmpl.DMA_DIR                = DMA_DIR_MemoryToPeripheral;
    s_dma_tmpl.DMA_BufferSize         = 1;    /* 每次TxStart时设置 */
    s_dma_tmpl.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    s_dma_tmpl.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    s_dma_tmpl.DMA_PeripheralDataSize = cfg->psize;
    s_dma_tmpl.DMA_MemoryDataSize     = cfg->msize;
    s_dma_tmpl.DMA_Mode               = DMA_Mode_Normal;
    s_dma_tmpl.DMA_Priority           = DMA_Priority_High;
    s_dma_tmpl.DMA_FIFOMode           = DMA_FIFOMode_Disable;
    s_dma_tmpl.DMA_FIFOThreshold      = DMA_FIFOThreshold_Full;
    s_dma_tmpl.DMA_MemoryBurst        = DMA_MemoryBurst_Single;
    s_dma_tmpl.DMA_PeripheralBurst    = DMA_PeripheralBurst_Single;
    DMA_Init(s_stream, &s_dma_tmpl);

    /* 3.传输完成中断使能 + 清历史标志 + NVIC(优先级由板级配置注入) */
    no = dma_stream_no(s_stream);
    DMA_ITConfig(s_stream, DMA_IT_TC, ENABLE);
    DMA_ClearITPendingBit(s_stream, s_flag_tc[no] | s_flag_te[no]);

    nvic_struct.NVIC_IRQChannel = cfg->irq_channel;
    nvic_struct.NVIC_IRQChannelPreemptionPriority = cfg->irq_preempt;
    nvic_struct.NVIC_IRQChannelSubPriority = cfg->irq_sub;
    nvic_struct.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic_struct);
}

uint8_t HAL_DMA_TxStart(const void *mem, uint16_t len)
{
    DMA_InitTypeDef dma_struct;

    if (s_stream == NULL || mem == NULL || len == 0)
        return 0;
    if (DMA_GetCmdStatus(s_stream) != DISABLE)   /* 上一传输未完成 */
        return 0;

    /* 基于Init模板, 仅更新内存地址与长度后重启 */
    DMA_Cmd(s_stream, DISABLE);
    dma_struct = s_dma_tmpl;
    dma_struct.DMA_Memory0BaseAddr = (uint32_t)mem;
    dma_struct.DMA_BufferSize      = len;
    DMA_Init(s_stream, &dma_struct);
    DMA_Cmd(s_stream, ENABLE);
    return 1;
}

uint8_t HAL_DMA_TxBusy(void)
{
    return (s_stream != NULL &&
            DMA_GetCmdStatus(s_stream) != DISABLE) ? 1 : 0;
}

void HAL_DMA_IRQHandler(void)
{
    uint8_t no;

    if (s_stream == NULL)
        return;

    no = dma_stream_no(s_stream);
    if (no == 0xFF)
        return;

    /* 传输完成 */
    if (DMA_GetITStatus(s_stream, s_flag_tc[no]) != RESET)
    {
        DMA_ClearITPendingBit(s_stream, s_flag_tc[no]);
        if (s_tc_cb != NULL)
            s_tc_cb();
    }
    /* 传输错误: 清标志, 上层通过超时自恢复 */
    if (DMA_GetITStatus(s_stream, s_flag_te[no]) != RESET)
    {
        DMA_ClearITPendingBit(s_stream, s_flag_te[no]);
    }
}
