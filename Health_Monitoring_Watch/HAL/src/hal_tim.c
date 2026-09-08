/**
 * @file    hal_tim.c
 * @brief   HAL层通用定时器驱动实现(更新中断回调分发 + PWM输出)
 */
#include "hal_tim.h"
#include "hal_gpio.h"

/* 回调注册表容量(足够覆盖本工程使用的定时器实例) */
#define HAL_TIM_MAX_INSTANCE    4

/* 实例回调注册表项 */
typedef struct {
    TIM_TypeDef         *instance;   /* 定时器实例 */
    HAL_TIM_UpdateCb_t    update_cb;  /* 更新中断回调 */
} HAL_TIM_Reg_t;

static HAL_TIM_Reg_t s_tim_reg[HAL_TIM_MAX_INSTANCE];
static uint8_t s_tim_reg_num = 0;

/* 查找实例的回调注册项(未注册返回NULL) */
static HAL_TIM_Reg_t *tim_reg_find(TIM_TypeDef *tim)
{
    uint8_t i;

    for (i = 0; i < s_tim_reg_num; i++)
    {
        if (s_tim_reg[i].instance == tim)
        {
            return &s_tim_reg[i];
        }
    }
    return (HAL_TIM_Reg_t *)0;
}

/* 查询定时器挂载的APB总线与NVIC中断通道(STM32F4固定映射) */
static uint8_t tim_apb_bus(TIM_TypeDef *tim)
{
    /* APB2: TIM1/TIM8/TIM9/TIM10/TIM11, 其余在APB1 */
    if ((tim == TIM1) || (tim == TIM8) || (tim == TIM9)
        || (tim == TIM10) || (tim == TIM11))
    {
        return 2;
    }
    return 1;
}

static uint8_t tim_irq_channel(TIM_TypeDef *tim)
{
    /* 按启动文件向量表映射(更新中断向量) */
    if (tim == TIM1)  { return TIM1_UP_TIM10_IRQn; }
    if (tim == TIM2)  { return TIM2_IRQn; }
    if (tim == TIM3)  { return TIM3_IRQn; }
    if (tim == TIM4)  { return TIM4_IRQn; }
    if (tim == TIM5)  { return TIM5_IRQn; }
    if (tim == TIM6)  { return TIM6_DAC_IRQn; }
    if (tim == TIM7)  { return TIM7_IRQn; }
    if (tim == TIM8)  { return TIM8_UP_TIM13_IRQn; }

    return 0xFF;   /* 未支持的实例 */
}

void HAL_TIM_BaseInit(const HAL_TIM_Config_t *cfg)
{
    TIM_TimeBaseInitTypeDef tim_struct;
    NVIC_InitTypeDef        nvic_struct;
    uint8_t                 irq_channel;

    if (cfg == NULL)
    {
        return;
    }

    irq_channel = tim_irq_channel(cfg->instance);
    if (irq_channel == 0xFF)
    {
        return;
    }

    /* 1. 使能定时器时钟 */
    if (tim_apb_bus(cfg->instance) == 2)
    {
        RCC_APB2PeriphClockCmd(cfg->clk, ENABLE);
    }
    else
    {
        RCC_APB1PeriphClockCmd(cfg->clk, ENABLE);
    }

    /* 2. 基本定时参数(内部时钟, 向上计数) */
    tim_struct.TIM_Period        = cfg->period;
    tim_struct.TIM_Prescaler     = cfg->prescaler;
    tim_struct.TIM_ClockDivision = TIM_CKD_DIV1;
    tim_struct.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(cfg->instance, &tim_struct);

    /* 3. NVIC更新中断配置 */
    nvic_struct.NVIC_IRQChannel                   = irq_channel;
    nvic_struct.NVIC_IRQChannelPreemptionPriority = cfg->irq_preempt;
    nvic_struct.NVIC_IRQChannelSubPriority        = cfg->irq_sub;
    nvic_struct.NVIC_IRQChannelCmd                 = ENABLE;
    NVIC_Init(&nvic_struct);

    /* 4. 登记回调注册表 */
    if (tim_reg_find(cfg->instance) == (HAL_TIM_Reg_t *)0
        && s_tim_reg_num < HAL_TIM_MAX_INSTANCE)
    {
        s_tim_reg[s_tim_reg_num].instance = cfg->instance;
        s_tim_reg[s_tim_reg_num].update_cb = cfg->update_cb;
        s_tim_reg_num++;
    }

    /* 5. 使能更新中断并启动计数 */
    TIM_ClearFlag(cfg->instance, TIM_FLAG_Update);  /* 清除TimeBaseInit置位的中断标志, 防止上电误触发 */
    TIM_ITConfig(cfg->instance, TIM_IT_Update, ENABLE);
    TIM_Cmd(cfg->instance, ENABLE);
}

void HAL_TIM_IRQHandler(TIM_TypeDef *tim)
{
    HAL_TIM_Reg_t *reg = tim_reg_find(tim);

    if (TIM_GetITStatus(tim, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(tim, TIM_IT_Update);

        if ((reg != (HAL_TIM_Reg_t *)0) && (reg->update_cb != (HAL_TIM_UpdateCb_t)0))
        {
            reg->update_cb();
        }
    }
}

/* ==================== PWM输出 ==================== */

/* 按通道选择比较寄存器: 装入初始比较值并使能预装载 */
static void tim_oc_init(TIM_TypeDef *tim, uint8_t channel,
                        TIM_OCInitTypeDef *oc)
{
    switch (channel)
    {
    case 1:
        TIM_OC1Init(tim, oc);
        TIM_OC1PreloadConfig(tim, TIM_OCPreload_Enable);
        break;
    case 2:
        TIM_OC2Init(tim, oc);
        TIM_OC2PreloadConfig(tim, TIM_OCPreload_Enable);
        break;
    case 3:
        TIM_OC3Init(tim, oc);
        TIM_OC3PreloadConfig(tim, TIM_OCPreload_Enable);
        break;
    case 4:
        TIM_OC4Init(tim, oc);
        TIM_OC4PreloadConfig(tim, TIM_OCPreload_Enable);
        break;
    default:
        break;
    }
}

void HAL_TIM_PWMInit(const HAL_TIM_PWMConfig_t *cfg)
{
    TIM_TimeBaseInitTypeDef tim_struct;
    TIM_OCInitTypeDef       oc_struct;

    if (cfg == NULL)
    {
        return;
    }

    /* 1. 使能定时器时钟 */
    if (tim_apb_bus(cfg->instance) == 2)
    {
        RCC_APB2PeriphClockCmd(cfg->clk, ENABLE);
    }
    else
    {
        RCC_APB1PeriphClockCmd(cfg->clk, ENABLE);
    }

    /* 2. PWM输出引脚(复用推挽, 复用参数由BSP配置注入) */
    HAL_GPIO_Init(&cfg->gpio);

    /* 3. 时基: 内部时钟向上计数 */
    tim_struct.TIM_Period        = cfg->period;
    tim_struct.TIM_Prescaler     = cfg->prescaler;
    tim_struct.TIM_ClockDivision = TIM_CKD_DIV1;
    tim_struct.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(cfg->instance, &tim_struct);

    /* 4. 比较通道: PWM1模式, 高电平有效 */
    TIM_OCStructInit(&oc_struct);
    oc_struct.TIM_OCMode      = TIM_OCMode_PWM1;
    oc_struct.TIM_OutputState = TIM_OutputState_Enable;
    oc_struct.TIM_Pulse       = cfg->pulse;
    oc_struct.TIM_OCPolarity  = TIM_OCPolarity_High;
    tim_oc_init(cfg->instance, cfg->channel, &oc_struct);

    /* 5. 启动计数(ARR预装载默认使能, 比较值即时生效经预装载寄存器) */
    TIM_ARRPreloadConfig(cfg->instance, ENABLE);
    TIM_Cmd(cfg->instance, ENABLE);
}

void HAL_TIM_PWMSetPulse(TIM_TypeDef *tim, uint8_t channel, uint32_t pulse)
{
    switch (channel)
    {
    case 1: TIM_SetCompare1(tim, pulse); break;
    case 2: TIM_SetCompare2(tim, pulse); break;
    case 3: TIM_SetCompare3(tim, pulse); break;
    case 4: TIM_SetCompare4(tim, pulse); break;
    default: break;
    }
}
