/*
 * ir.h: IR transmitter (PB1, 38 kHz carrier from TIM8_CH3N) and the
 * demodulating IR receiver (PF8, active low, EXTI8).
 * Pins: include/board_pins.h (pinmap.md "Indicators and outputs").
 *
 * M2 use: a DIAG loopback self-test (PRD inventory: "DIAG IR loopback") and
 * a listen mode that shows the pulses of any IR remote.
 */
#ifndef IR_H
#define IR_H

#include <stdbool.h>
#include <stdint.h>

#include "status.h"

typedef struct {
    unsigned bursts;        /* carrier bursts sent */
    unsigned detected;      /* bursts with a receiver LOW pulse starting inside them */
    unsigned extra_edges;   /* falling edges not matched to a burst */
    uint32_t mean_delay_us; /* burst start -> receiver output low (detected bursts) */
    uint32_t mean_low_us;   /* receiver low-pulse width (detected bursts) */
    unsigned idle_edges;    /* receiver edges during the 50 ms quiet window before */
} ir_loop_result_t;

typedef struct {
    unsigned edges;
    unsigned frames;              /* edge groups separated by > 20 ms */
    uint32_t first_low_us[8];     /* first low pulse of each of the first 8 frames */
    unsigned frame_edges[8];      /* edges in each of the first 8 frames */
    bool overflow;
} ir_listen_result_t;

/* TX pin + TIM8 (carrier off), RX pin + EXTI8. Call from a task. */
status_t ir_init(void);
bool ir_is_initialised(void);
uint32_t ir_carrier_hz(void);

/* Send `bursts` carrier bursts of on_us, separated by off_us, and match the
   receiver's pulses to them. Busy-waits for bursts x (on_us + off_us). */
status_t ir_loopback(unsigned bursts, uint32_t on_us, uint32_t off_us, ir_loop_result_t *r);

/* Control for the loopback: false = PB1 becomes a plain GPIO output held low
   (the timer keeps running, the LED stays dark); true = PB1 back on TIM8. */
void ir_tx_connect(bool connected);

/* Record receiver edges for `ms` milliseconds (sleeps; the ISR records). */
status_t ir_listen(uint32_t ms, ir_listen_result_t *r);

/* EXTI9_5 shares: call from EXTI9_5_IRQHandler. Ignores other lines. */
void ir_exti_irq(void);

#endif /* IR_H */
