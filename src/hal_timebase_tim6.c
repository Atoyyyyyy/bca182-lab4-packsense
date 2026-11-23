/*
 * hal_timebase_tim6.c: HAL time base on TIM6 instead of SysTick.
 *
 * SysTick belongs to FreeRTOS only (PRD 6.1). HAL_Init() and
 * HAL_RCC_ClockConfig() call HAL_InitTick(), which these strong definitions
 * replace. Based on ST's stm32f4xx_hal_timebase_tim_template.c (STM32CubeF4
 * 1.28.3), with the IRQ priority set before the IRQ is enabled, and the IRQ
 * handled directly (src/stm32f4xx_it.c) instead of through HAL callbacks.
 */
#include "stm32f4xx_hal.h"

static TIM_HandleTypeDef s_htim6;

HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
    RCC_ClkInitTypeDef clk;
    uint32_t flash_latency;
    uint32_t tim_clk;

    if (TickPriority >= (1UL << __NVIC_PRIO_BITS)) {
        return HAL_ERROR;
    }

    __HAL_RCC_TIM6_CLK_ENABLE();
    HAL_NVIC_DisableIRQ(TIM6_DAC_IRQn);

    /* TIM6 sits on APB1. Timer clock = PCLK1, or 2 x PCLK1 if APB1 is divided. */
    HAL_RCC_GetClockConfig(&clk, &flash_latency);
    tim_clk = HAL_RCC_GetPCLK1Freq();
    if (clk.APB1CLKDivider != RCC_HCLK_DIV1) {
        tim_clk *= 2U;
    }

    /* 1 MHz counter, update event every 1000 / uwTickFreq counts (1 ms). */
    s_htim6.Instance = TIM6;
    s_htim6.Init.Prescaler = (tim_clk / 1000000U) - 1U;
    s_htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
    s_htim6.Init.Period = (1000U * (uint32_t)uwTickFreq) - 1U;
    s_htim6.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    s_htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_Base_Init(&s_htim6) != HAL_OK) {
        return HAL_ERROR;
    }

    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, TickPriority, 0U);
    uwTickPrio = TickPriority;

    if (HAL_TIM_Base_Start_IT(&s_htim6) != HAL_OK) {
        return HAL_ERROR;
    }
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
    return HAL_OK;
}

void HAL_SuspendTick(void)
{
    __HAL_TIM_DISABLE_IT(&s_htim6, TIM_IT_UPDATE);
}

void HAL_ResumeTick(void)
{
    __HAL_TIM_ENABLE_IT(&s_htim6, TIM_IT_UPDATE);
}

/* Called from TIM6_DAC_IRQHandler (priority TICK_INT_PRIORITY = 4).
   No FreeRTOS calls allowed here. */
void hal_timebase_irq(void)
{
    if ((TIM6->SR & TIM_SR_UIF) != 0U) {
        TIM6->SR = ~TIM_SR_UIF;
        HAL_IncTick();
    }
}
