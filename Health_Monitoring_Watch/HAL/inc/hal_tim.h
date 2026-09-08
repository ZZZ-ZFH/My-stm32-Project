/**
 * @file    hal_tim.h
 * @brief   HAL层通用基本定时器驱动: 更新中断+回调框架
 *          与具体板级硬件无关, 参数由BSP层注入
 *
 * 接收设计(中断驱动):
 *   - 更新中断到达时调用注册的 update_cb(中断上下文, 须短小非阻塞)
 *   - 中断向量函数(如TIM3_IRQHandler)由BSP层定义,
 *     转调 HAL_TIM_IRQHandler(instance) 完成通用分发
 */
#ifndef __HAL_TIM_H
#define __HAL_TIM_H

#include "stm32f4xx.h"
#include "stdio.h"
#include "hal_gpio.h"
#ifdef __cplusplus
extern "C" {
#endif

/* 更新中断回调类型(中断上下文, 须短小非阻塞) */
typedef void (*HAL_TIM_UpdateCb_t)(void);

/* 定时器通用配置结构体(BSP层填充板级参数) */
typedef struct {
    TIM_TypeDef         *instance;     /* TIM1~TIM14 */
    uint32_t             clk;          /* 外设时钟: RCC_APBxPeriph_TIMx */
    uint16_t             prescaler;    /* 预分频(计数值-1) */
    uint16_t             period;       /* 自动重装载(计数值-1) */
    uint8_t              irq_preempt;  /* 抢占优先级 */
    uint8_t              irq_sub;      /* 响应优先级 */
    HAL_TIM_UpdateCb_t   update_cb;     /* 更新中断回调 */
} HAL_TIM_Config_t;

/* HAL层通用定时器接口 */
void HAL_TIM_BaseInit(const HAL_TIM_Config_t *cfg);   /* 初始化基本定时器(含NVIC+更新中断+启动) */
void HAL_TIM_IRQHandler(TIM_TypeDef *tim);            /* 通用中断处理(BSP中断向量函数转调) */

/* ==================== PWM输出扩展 ====================
 * 用于背光/蜂鸣等调功场景: 定时器PWM单通道输出, 无中断 */

/* PWM输出配置结构体(BSP层填充板级参数) */
typedef struct {
    TIM_TypeDef         *instance;     /* TIM1~TIM14 */
    uint32_t             clk;          /* 外设时钟: RCC_APBxPeriph_TIMx */
    uint16_t             prescaler;    /* 预分频(计数值-1) */
    uint16_t             period;       /* 自动重装载(计数值-1) */
    uint8_t              channel;      /* 输出通道: 1~4 (TIM_OCx) */
    HAL_GPIO_Config_t    gpio;         /* PWM输出引脚(复用推挽) */
    uint32_t             pulse;        /* 初始比较值(占空比) */
} HAL_TIM_PWMConfig_t;

/* PWM输出接口 */
void HAL_TIM_PWMInit(const HAL_TIM_PWMConfig_t *cfg);           /* 初始化PWM输出(引脚+时基+比较通道+启动) */
void HAL_TIM_PWMSetPulse(TIM_TypeDef *tim, uint8_t channel,
                         uint32_t pulse);                        /* 修改比较值(占空比) */

#ifdef __cplusplus
}
#endif

#endif /* __HAL_TIM_H */
