/*
 * ui.h: UiTask (PRD 6.3, priority 3, 50 ms): boot self-test, mode changes,
 * keys, LCD / LED / LED matrix / buzzer, alerts and the climate rule.
 *
 * Keys in RUN / GUARD:  LEFT held = arm / disarm GUARD, UP = DIAG,
 *                       DOWN held = mute / unmute the buzzer, RIGHT = clear banner;
 *                       while the guard alarm sounds, any key acknowledges it.
 * Keys in DIAG:         LEFT / RIGHT = page, UP = back, DOWN = page action.
 * Every key press is echoed in the LCD footer at once (AC-5) and logged as
 * `[key] ... edge_to_feedback_ms=...`.
 *
 * Alerts (PRD 2.3), on a change of the smoothed state with confidence >= 0.5:
 *   ROUGH          chirp, orange LED matrix, "Rough handling detected"
 *   ACCESS, GUARD  alarm until a key: buzzer pattern, red LED, red screen
 *   ACCESS, RUN    logged only
 *   climate        temperature or RH above threshold for CLIMATE_HOLD_S
 */
#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include <stdint.h>

/* reset_csr: RCC->CSR at boot (event log). capture_key: UP held at reset. */
void ui_task_create(uint32_t reset_csr, bool capture_key);

/* Window-close -> feedback latency over the last 128 windows (AC-4). */
void ui_get_latency(uint32_t *n, uint32_t *med, uint32_t *p95, uint32_t *mx);

#endif /* UI_H */
