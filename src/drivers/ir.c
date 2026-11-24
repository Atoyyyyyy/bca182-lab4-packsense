/*
 * ir.c: IR carrier on TIM8_CH3N (PB1) and receiver edge capture on EXTI8 (PF8).
 *
 * Carrier: TIM8 counts at 168 MHz (APB2 timer clock), ARR = 4420 ->
 * 38.00 kHz; duty 1/3 (CCR3 = ARR / 3), the values the board vendor's IR
 * demo uses (26316 ns period, 8770 ns pulse). Only the complementary output
 * CH3N is enabled: with CC3E = 0, CC3NE = 1 and MOE = 1 the pin follows OC3REF,
 * so CCR3 = 0 keeps the LED off. CCR3 preload switches the carrier on/off on
 * a carrier-period boundary.
 *
 * Receiver: every edge on PF8 is time-stamped with the DWT cycle counter in
 * the ISR (ring of 256), and the task-level code interprets them.
 */
#include "ir.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "app_config.h"
#include "board_pins.h"

#define CARRIER_HZ  38000UL
#define RING        256U
#define FRAME_GAP_US 20000U

typedef struct {
    uint32_t cyc;
    uint8_t level; /* pin level after the edge: 0 = low (IR seen) */
} ir_edge_t;

static volatile ir_edge_t s_ring[RING];
static volatile uint32_t s_head;   /* edges written (wraps) */
static bool s_ready;
static uint32_t s_arr;

static uint32_t tim8_clock_hz(void)
{
    const uint32_t pclk2 = HAL_RCC_GetPCLK2Freq();
    return ((RCC->CFGR & RCC_CFGR_PPRE2) == RCC_CFGR_PPRE2_DIV1) ? pclk2 : 2U * pclk2;
}

static inline void carrier(bool on)
{
    TIM8->CCR3 = on ? (s_arr + 1U) / 3U : 0U;
}

status_t ir_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_TIM8_CLK_ENABLE();
    __HAL_RCC_SYSCFG_CLK_ENABLE();

    /* TX: timer first (output low), then hand the pin to it. */
    s_arr = tim8_clock_hz() / CARRIER_HZ - 1U;
    TIM8->CR1 = TIM_CR1_ARPE;
    TIM8->PSC = 0U;
    TIM8->ARR = s_arr;
    TIM8->CCR3 = 0U;
    TIM8->CCMR2 = (TIM8->CCMR2 & ~(TIM_CCMR2_OC3M | TIM_CCMR2_CC3S)) | (6U << TIM_CCMR2_OC3M_Pos) |
                  TIM_CCMR2_OC3PE;
    TIM8->CCER = (TIM8->CCER & ~(TIM_CCER_CC3E | TIM_CCER_CC3P | TIM_CCER_CC3NP)) | TIM_CCER_CC3NE;
    TIM8->BDTR = TIM_BDTR_MOE; /* no dead time, no break */
    TIM8->EGR = TIM_EGR_UG;
    TIM8->CR1 |= TIM_CR1_CEN;

    gpio.Pin = IR_TX_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLDOWN;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = IR_TX_AF;
    HAL_GPIO_Init(IR_TX_PORT, &gpio);

    /* RX: both edges on EXTI8. The receiver drives the line; the pull-up only
       matters if it is missing. */
    s_head = 0U;
    gpio.Pin = IR_RX_PIN;
    gpio.Mode = GPIO_MODE_IT_RISING_FALLING;
    gpio.Pull = GPIO_PULLUP;
    gpio.Alternate = 0U;
    HAL_GPIO_Init(IR_RX_PORT, &gpio);
    __HAL_GPIO_EXTI_CLEAR_IT(IR_RX_PIN);
    HAL_NVIC_SetPriority(EXTI9_5_IRQn, IRQ_PRIO_BUTTONS, 0); /* same line as key UP */
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

    s_ready = true;
    return ST_OK;
}

void ir_tx_connect(bool connected)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = IR_TX_PIN;
    gpio.Pull = GPIO_PULLDOWN;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    if (connected) {
        gpio.Mode = GPIO_MODE_AF_PP;
        gpio.Alternate = IR_TX_AF;
    } else {
        HAL_GPIO_WritePin(IR_TX_PORT, IR_TX_PIN, GPIO_PIN_RESET);
        gpio.Mode = GPIO_MODE_OUTPUT_PP;
    }
    HAL_GPIO_Init(IR_TX_PORT, &gpio);
}

bool ir_is_initialised(void)
{
    return s_ready;
}

uint32_t ir_carrier_hz(void)
{
    return s_ready ? tim8_clock_hz() / (s_arr + 1U) : 0U;
}

void ir_exti_irq(void)
{
    if ((EXTI->PR & IR_RX_PIN) == 0U) {
        return;
    }
    EXTI->PR = IR_RX_PIN;
    const uint32_t h = s_head; /* readers detect overrun from head - tail */
    s_ring[h % RING].cyc = DWT->CYCCNT;
    s_ring[h % RING].level = (IR_RX_PORT->IDR & IR_RX_PIN) ? 1U : 0U;
    s_head = h + 1U;
}

static void busy_us(uint32_t us)
{
    const uint32_t c0 = DWT->CYCCNT;
    const uint32_t n = us * (SystemCoreClock / 1000000U);
    while ((DWT->CYCCNT - c0) < n) {
    }
}

#define MAX_BURSTS 64U

status_t ir_loopback(unsigned bursts, uint32_t on_us, uint32_t off_us, ir_loop_result_t *r)
{
    static uint32_t start[MAX_BURSTS];
    memset(r, 0, sizeof *r);
    if (!s_ready) {
        return ST_NOT_PRESENT;
    }
    if (bursts == 0U || bursts > MAX_BURSTS) {
        return ST_RANGE_ERR;
    }
    const uint32_t per_us = SystemCoreClock / 1000000U;

    /* Quiet window: edges here are ambient IR / noise. */
    uint32_t h0 = s_head;
    vTaskDelay(pdMS_TO_TICKS(50));
    r->idle_edges = (unsigned)(s_head - h0);

    h0 = s_head;
    for (unsigned b = 0; b < bursts; b++) {
        start[b] = DWT->CYCCNT;
        carrier(true);
        busy_us(on_us);
        carrier(false);
        busy_us(off_us);
    }
    busy_us(2000U); /* let the last receiver pulse end */
    const uint32_t h1 = s_head;
    r->bursts = bursts;
    if (h1 - h0 > RING) {
        return ST_RANGE_ERR;
    }

    /* Match each falling edge (level 0) to the burst it starts in (or up to
       400 us after it: the receiver's response delay). */
    uint64_t sum_delay = 0U, sum_low = 0U;
    unsigned next_burst = 0U;
    for (uint32_t i = h0; i < h1; i++) {
        const ir_edge_t e = s_ring[i % RING];
        if (e.level != 0U) {
            continue;
        }
        bool matched = false;
        while (next_burst < bursts) {
            const uint32_t d = (e.cyc - start[next_burst]) / per_us;
            if (d > 0x7FFFFFFFUL) {
                break; /* edge before this burst: noise */
            }
            if (d <= on_us + 400U) {
                matched = true;
                sum_delay += d;
                /* Low-pulse width: to the next rising edge. */
                for (uint32_t k = i + 1U; k < h1; k++) {
                    const ir_edge_t f = s_ring[k % RING];
                    if (f.level != 0U) {
                        sum_low += (f.cyc - e.cyc) / per_us;
                        break;
                    }
                }
                next_burst++;
                break;
            }
            next_burst++; /* this burst got no pulse */
        }
        if (matched) {
            r->detected++;
        } else {
            r->extra_edges++;
        }
    }
    if (r->detected) {
        r->mean_delay_us = (uint32_t)(sum_delay / r->detected);
        r->mean_low_us = (uint32_t)(sum_low / r->detected);
    }
    return ST_OK;
}

status_t ir_listen(uint32_t ms, ir_listen_result_t *r)
{
    memset(r, 0, sizeof *r);
    if (!s_ready) {
        return ST_NOT_PRESENT;
    }
    const uint32_t per_us = SystemCoreClock / 1000000U;
    uint32_t i = s_head;
    uint32_t last_cyc = 0U;
    bool have_last = false;
    bool in_frame = false;
    bool want_first_low = false;
    const TickType_t t0 = xTaskGetTickCount();

    while ((xTaskGetTickCount() - t0) < pdMS_TO_TICKS(ms)) {
        vTaskDelay(pdMS_TO_TICKS(5));
        const uint32_t h = s_head;
        if (h - i > RING) {
            r->overflow = true;
            i = h - RING;
        }
        for (; i != h; i++) {
            const ir_edge_t e = s_ring[i % RING];
            r->edges++;
            if (!have_last || (e.cyc - last_cyc) / per_us > FRAME_GAP_US) {
                in_frame = true;
                if (r->frames < 8U) {
                    want_first_low = (e.level == 0U);
                }
                r->frames++;
            } else if (in_frame && want_first_low && e.level != 0U && r->frames <= 8U) {
                r->first_low_us[r->frames - 1U] = (e.cyc - last_cyc) / per_us;
                want_first_low = false;
            }
            if (r->frames >= 1U && r->frames <= 8U) {
                r->frame_edges[r->frames - 1U]++;
            }
            last_cyc = e.cyc;
            have_last = true;
        }
    }
    return ST_OK;
}
