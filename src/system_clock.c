/*
 * system_clock.c: 168 MHz system clock from the 8 MHz HSE crystal.
 *
 * Board fact: X2 = 8 MHz crystal on PH0/PH1 (schematic sheet 5/10, pinmap.md).
 * PLL: VCO in = HSE / M = 8 / 8 = 1 MHz, VCO out = 1 x N = 336 MHz,
 *      SYSCLK = 336 / P(2) = 168 MHz, PLL48CLK = 336 / Q(7) = 48 MHz.
 * Buses: AHB /1 = 168 MHz, APB1 /4 = 42 MHz (max 42), APB2 /2 = 84 MHz (max 84).
 * Flash: 5 wait states at 168 MHz and VDD 2.7..3.6 V (RM0090 table 10).
 *
 * The boot banner prints the PLL registers as read back from RCC, so the boot
 * log shows the configuration that is actually running.
 */
#include "system_clock.h"

#include "stm32f4xx_hal.h"

static HAL_StatusTypeDef configure_pll(uint32_t source, uint32_t pllm)
{
    RCC_OscInitTypeDef osc = {0};

    if (source == RCC_PLLSOURCE_HSE) {
        osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
        osc.HSEState = RCC_HSE_ON; /* crystal, not bypass */
    } else {
        osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
        osc.HSIState = RCC_HSI_ON;
        osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    }
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = source;
    osc.PLL.PLLM = pllm;
    osc.PLL.PLLN = 336U;
    osc.PLL.PLLP = RCC_PLLP_DIV2;
    osc.PLL.PLLQ = 7U;
    return HAL_RCC_OscConfig(&osc);
}

static HAL_StatusTypeDef switch_to_pll(void)
{
    RCC_ClkInitTypeDef clk = {0};

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;
    return HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5);
}

clk_source_t system_clock_config(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    /* HSE 8 MHz / M 8 = 1 MHz VCO input. */
    if (configure_pll(RCC_PLLSOURCE_HSE, 8U) == HAL_OK && switch_to_pll() == HAL_OK) {
        return CLK_SRC_HSE_PLL;
    }

    /* HSE did not start (or PLL did not lock): turn it off, use HSI 16 MHz / 16. */
    RCC_OscInitTypeDef off = {0};
    off.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    off.HSEState = RCC_HSE_OFF;
    off.PLL.PLLState = RCC_PLL_NONE;
    (void)HAL_RCC_OscConfig(&off);

    if (configure_pll(RCC_PLLSOURCE_HSI, 16U) == HAL_OK && switch_to_pll() == HAL_OK) {
        return CLK_SRC_HSI_PLL;
    }
    return CLK_SRC_FAILED;
}

const char *system_clock_source_name(clk_source_t src)
{
    switch (src) {
    case CLK_SRC_HSE_PLL:
        return "HSE 8 MHz crystal -> PLL";
    case CLK_SRC_HSI_PLL:
        return "HSI 16 MHz -> PLL (FALLBACK: HSE did not start)";
    default:
        return "HSI 16 MHz, no PLL (CLOCK CONFIG FAILED)";
    }
}
