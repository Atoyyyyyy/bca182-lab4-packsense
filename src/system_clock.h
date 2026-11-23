#ifndef SYSTEM_CLOCK_H
#define SYSTEM_CLOCK_H

#include <stdint.h>

typedef enum {
    CLK_SRC_HSE_PLL = 0,    /* normal: 8 MHz crystal X2 -> PLL -> 168 MHz */
    CLK_SRC_HSI_PLL = 1,    /* fallback: HSE failed to start */
    CLK_SRC_FAILED = 2      /* no PLL lock: still on the 16 MHz HSI */
} clk_source_t;

/* Sets SYSCLK = 168 MHz, AHB 168, APB1 42, APB2 84, 48 MHz for USB/SDIO.
   Falls back to the HSI if the HSE does not start. */
clk_source_t system_clock_config(void);

const char *system_clock_source_name(clk_source_t src);

#endif /* SYSTEM_CLOCK_H */
