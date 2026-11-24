/*
 * led_matrix.c: SK6805 chain on PA7, driven by TIM3 channel 2 PWM with the
 * duty cycle of every bit period loaded by DMA1 stream 5 (TIM3_CH2 request).
 *
 * Bit timing follows the board vendor's driver for this matrix
 * (RT-Thread-Studio/sdk-bsp-stm32f407-spark, libraries/Board_Drivers/
 * led_matrix/drv_matrix_led.c at 4c284fc), because the SK6805 datasheet is
 * not in docs/hardware/vendor yet: TIM3 at 84 MHz / 10 = 8.4 MHz, 10 counts
 * per bit (1.19 us, 840 kHz), a 0 bit is high for 2 counts (238 ns), a 1 bit
 * for 7 counts (833 ns). 24 bits per LED, green-red-blue, MSB first.
 *
 * Sequence: CCR2 starts at 0 (line low). With output-compare preload on, the
 * DMA write made at the compare event of period k takes effect in period
 * k + 1, so the line idles low for one period, then carries the bits. The
 * buffer ends with two 0 entries, so the line is low again when the DMA
 * finishes and the timer can be stopped. The gap until the next frame
 * (at least one tick, 1 ms) is the latch/reset time.
 */
#include "led_matrix.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "board_pins.h"

#define TIM_TICK_HZ   8400000UL
#define BIT_COUNTS    10U
#define DUTY_0        2U
#define DUTY_1        7U
#define TAIL          2U
#define BITS_PER_LED  24U
#define NBITS         (LEDMX_COUNT * BITS_PER_LED)
#define NBUF          (NBITS + TAIL)

#define DMA_STREAM    DMA1_Stream5
#define DMA_CHANNEL   5U
#define DMA_TC_FLAG   DMA_HISR_TCIF5
#define DMA_TE_FLAG   DMA_HISR_TEIF5
#define DMA_ALL_FLAGS (DMA_HIFCR_CTCIF5 | DMA_HIFCR_CHTIF5 | DMA_HIFCR_CTEIF5 | DMA_HIFCR_CDMEIF5 | DMA_HIFCR_CFEIF5)

static uint8_t s_rgb[LEDMX_COUNT][3];
static uint16_t s_buf[NBUF]; /* CCR2 value per bit period */
static bool s_ready;
static led_matrix_stats_t s_stats;

static uint32_t tim3_clock_hz(void)
{
    /* APB1 timers run at 2 x PCLK1 when the APB1 prescaler is not 1. */
    const uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    return ((RCC->CFGR & RCC_CFGR_PPRE1) == RCC_CFGR_PPRE1_DIV1) ? pclk1 : 2U * pclk1;
}

status_t led_matrix_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    const uint32_t timclk = tim3_clock_hz();
    if (timclk % TIM_TICK_HZ != 0U) {
        return ST_RANGE_ERR; /* bit timing would be off */
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    /* Buffer on. */
    HAL_GPIO_WritePin(LEDMX_EN_PORT, LEDMX_EN_PIN, LEDMX_EN_ON_LEVEL);
    gpio.Pin = LEDMX_EN_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LEDMX_EN_PORT, &gpio);

    /* TIM3: 8.4 MHz tick, 10 counts per bit, PWM mode 1 on CH2 with preload,
       output low (CCR2 = 0) while idle. */
    TIM3->CR1 = 0U;
    TIM3->DIER = 0U;
    TIM3->PSC = (uint16_t)(timclk / TIM_TICK_HZ - 1U);
    TIM3->ARR = BIT_COUNTS - 1U;
    TIM3->CCR2 = 0U;
    TIM3->CCMR1 = (TIM3->CCMR1 & ~(TIM_CCMR1_OC2M | TIM_CCMR1_CC2S)) | (6U << TIM_CCMR1_OC2M_Pos) |
                  TIM_CCMR1_OC2PE;
    TIM3->CCER |= TIM_CCER_CC2E;
    TIM3->EGR = TIM_EGR_UG;
    TIM3->SR = 0U;

    gpio.Pin = LEDMX_DATA_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLDOWN;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = LEDMX_DATA_AF;
    HAL_GPIO_Init(LEDMX_DATA_PORT, &gpio);

    /* DMA1 stream 5, channel 5: memory -> TIM3->CCR2, 16-bit, one request
       per CC2 event. Polled, no interrupt. */
    DMA_STREAM->CR = 0U;
    while (DMA_STREAM->CR & DMA_SxCR_EN) {
    }
    DMA_STREAM->PAR = (uint32_t)&TIM3->CCR2;
    DMA_STREAM->FCR = 0U; /* direct mode */
    DMA_STREAM->CR = (DMA_CHANNEL << DMA_SxCR_CHSEL_Pos) | DMA_SxCR_PL_1 /* high */ |
                     DMA_SxCR_MSIZE_0 | DMA_SxCR_PSIZE_0 /* 16-bit */ | DMA_SxCR_MINC |
                     DMA_SxCR_DIR_0 /* memory to peripheral */;

    s_ready = true;
    memset(&s_stats, 0, sizeof s_stats);
    s_stats.initialised = true;
    led_matrix_fill(0, 0, 0);
    return led_matrix_show();
}

bool led_matrix_is_initialised(void)
{
    return s_ready;
}

void led_matrix_set(unsigned index, uint8_t r, uint8_t g, uint8_t b)
{
    if (index < LEDMX_COUNT) {
        s_rgb[index][0] = r;
        s_rgb[index][1] = g;
        s_rgb[index][2] = b;
    }
}

void led_matrix_fill(uint8_t r, uint8_t g, uint8_t b)
{
    for (unsigned i = 0; i < LEDMX_COUNT; i++) {
        led_matrix_set(i, r, g, b);
    }
}

static void encode(void)
{
    uint16_t *p = s_buf;
    for (unsigned i = 0; i < LEDMX_COUNT; i++) {
        const uint8_t grb[3] = {s_rgb[i][1], s_rgb[i][0], s_rgb[i][2]};
        for (unsigned c = 0; c < 3U; c++) {
            for (int bit = 7; bit >= 0; bit--) {
                *p++ = ((grb[c] >> bit) & 1U) ? DUTY_1 : DUTY_0;
            }
        }
    }
    for (unsigned t = 0; t < TAIL; t++) {
        *p++ = 0U;
    }
}

static void stop(void)
{
    TIM3->CR1 &= ~TIM_CR1_CEN;
    TIM3->DIER &= ~TIM_DIER_CC2DE;
    DMA_STREAM->CR &= ~DMA_SxCR_EN;
    while (DMA_STREAM->CR & DMA_SxCR_EN) {
    }
    TIM3->CCR2 = 0U;
    TIM3->EGR = TIM_EGR_UG; /* load CCR2 = 0: line stays low */
}

status_t led_matrix_show(void)
{
    if (!s_ready) {
        return ST_NOT_PRESENT;
    }
    encode();

    /* Timer stopped with the line low; restart from count 0. */
    TIM3->CR1 &= ~TIM_CR1_CEN;
    TIM3->CNT = 0U;
    TIM3->CCR2 = 0U;
    TIM3->EGR = TIM_EGR_UG;
    TIM3->SR = 0U;

    DMA1->HIFCR = DMA_ALL_FLAGS;
    DMA_STREAM->M0AR = (uint32_t)s_buf;
    DMA_STREAM->NDTR = NBUF;
    DMA_STREAM->CR |= DMA_SxCR_EN;

    const uint32_t c0 = DWT->CYCCNT;
    TIM3->DIER |= TIM_DIER_CC2DE;
    TIM3->CR1 |= TIM_CR1_CEN;

    /* ~0.55 ms on the wire: check every tick, give up after 10 ms. */
    status_t st = ST_TIMEOUT;
    const TickType_t t0 = xTaskGetTickCount();
    for (;;) {
        const uint32_t isr = DMA1->HISR;
        if (isr & DMA_TE_FLAG) {
            st = ST_BUS_ERR;
            break;
        }
        if (isr & DMA_TC_FLAG) {
            st = ST_OK;
            break;
        }
        if ((xTaskGetTickCount() - t0) > pdMS_TO_TICKS(10)) {
            break;
        }
        /* Spin for the first ~0.6 ms instead of sleeping a whole tick. */
        if ((DWT->CYCCNT - c0) > SystemCoreClock / 1000U) {
            vTaskDelay(1);
        }
    }
    const uint32_t cyc = DWT->CYCCNT - c0;
    stop();

    if (st == ST_OK) {
        s_stats.frames++;
        s_stats.last_wire_us = cyc / (SystemCoreClock / 1000000U);
    } else if (st == ST_TIMEOUT) {
        s_stats.timeouts++;
    } else {
        s_stats.dma_errors++;
    }
    return st;
}

void led_matrix_get_stats(led_matrix_stats_t *st)
{
    *st = s_stats;
    st->initialised = s_ready;
}
