/*
 * led_matrix.h: 19 x SK6805 serial RGB LEDs (WS2812-style one-wire protocol).
 * Pins: include/board_pins.h (pinmap.md "Indicators and outputs").
 *
 * Set colours in the frame buffer, then led_matrix_show() sends the whole
 * chain (about 0.55 ms on the wire). One task owns the matrix.
 */
#ifndef LED_MATRIX_H
#define LED_MATRIX_H

#include <stdbool.h>
#include <stdint.h>

#include "status.h"

typedef struct {
    bool initialised;
    uint32_t frames;       /* successful led_matrix_show() calls */
    uint32_t timeouts;     /* DMA did not finish in time */
    uint32_t dma_errors;   /* DMA transfer error flag seen */
    uint32_t last_wire_us; /* DMA start to transfer complete, last frame */
} led_matrix_stats_t;

/* TIM3 PWM + DMA, EN low (buffer on), all LEDs off. Call from a task. */
status_t led_matrix_init(void);
bool led_matrix_is_initialised(void);

/* Frame buffer only; nothing is sent until led_matrix_show(). */
void led_matrix_set(unsigned index, uint8_t r, uint8_t g, uint8_t b);
void led_matrix_fill(uint8_t r, uint8_t g, uint8_t b);

/* Send the frame buffer. Blocks for ~1-2 ticks; ST_TIMEOUT or ST_BUS_ERR on
   a DMA problem. */
status_t led_matrix_show(void);

void led_matrix_get_stats(led_matrix_stats_t *st);

#endif /* LED_MATRIX_H */
