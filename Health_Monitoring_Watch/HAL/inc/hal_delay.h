/**
 * @file    hal_delay.h
 * @brief   HAL层通用延时驱动: 基于SysTick的微秒/毫秒/秒延时(FreeRTOS兼容)
 *          换芯片时仅需修改实现文件(同为Cortex-M核可直接复用)
 */
#ifndef __HAL_DELAY_H
#define __HAL_DELAY_H

#include <stdint.h>

void HAL_Delay_Us(uint32_t nus);
void HAL_Delay_Ms(uint32_t nms);
void HAL_Delay_S(uint32_t ns);

#endif /* __HAL_DELAY_H */
