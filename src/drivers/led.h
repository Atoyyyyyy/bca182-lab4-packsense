/*
 * led.h: red and blue indicator LEDs (D4), active low.
 * Pins: include/board_pins.h (pinmap.md "Indicators and outputs").
 */
#ifndef LED_H
#define LED_H

#include <stdbool.h>

typedef enum {
    LED_RED = 0,
    LED_BLUE = 1,
    LED_COUNT
} led_id_t;

void led_init(void);           /* both LEDs off */
void led_set(led_id_t led, bool on);
void led_toggle(led_id_t led);
bool led_is_on(led_id_t led);

#endif /* LED_H */
