/*
 * buttons.h: the four arrow keys (PRD 6.2 buttons.c).
 *
 * The EXTI interrupt only timestamps each raw edge and posts it to a queue.
 * Debouncing happens in the task that calls buttons_get_event(): once a key
 * has been quiet for BUTTON_DEBOUNCE_MS, its pin level is read and compared
 * with the last stable state. A release reports how long the key was held and
 * whether that counts as a long press (>= BUTTON_LONG_PRESS_MS).
 *
 * One consumer task only (the CLI now, the UiTask from M5).
 */
#ifndef BUTTONS_H
#define BUTTONS_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"

typedef enum {
    BTN_UP = 0,
    BTN_DOWN,
    BTN_LEFT,
    BTN_RIGHT,
    BTN_COUNT
} button_id_t;

typedef struct {
    button_id_t key;
    bool pressed;     /* true = press, false = release */
    bool long_press;  /* release only: held >= BUTTON_LONG_PRESS_MS */
    uint32_t t_ms;    /* when the key settled (tick count) */
    uint32_t t_edge_ms; /* first raw EXTI edge of this change (tick count, ISR time) */
    uint32_t held_ms; /* release only */
    uint32_t edges;   /* raw edges seen for this change (1 = clean, more = bounce) */
} button_event_t;

typedef struct {
    uint32_t raw_edges; /* all edges counted by the ISR */
    uint32_t presses;
    uint32_t long_presses;
    uint32_t dropped;   /* raw edges lost because the queue was full */
    bool pressed;       /* current stable state */
} button_stats_t;

/* Pins, EXTI lines and the raw-edge queue. Interrupts are enabled here, so
   call it before the scheduler starts (the ISR only uses FromISR calls). */
void buttons_init(void);

/* Next debounced event. Waits up to `wait` ticks for the first edge.
   Returns false if nothing happened. */
bool buttons_get_event(button_event_t *ev, TickType_t wait);

/* Forget queued edges and re-read the stable state from the pins (use when a
   consumer starts listening after a pause). */
void buttons_resync(void);

void buttons_get_stats(button_id_t key, button_stats_t *out);
const char *button_name(button_id_t key);

/* Called from the EXTI IRQ handlers in stm32f4xx_it.c. */
void buttons_exti_irq(void);

#endif /* BUTTONS_H */
