/*
 * buzzer.h: active buzzer BUZ1 on PB0 (PRD 6.2 buzzer.c).
 *
 * It is a self-oscillating buzzer, so the pin only switches it on and off; the
 * pitch is fixed. Patterns (chirp / alarm / error) are played by a FreeRTOS
 * software timer, so buzzer_play() returns at once and never blocks the
 * caller.
 */
#ifndef BUZZER_H
#define BUZZER_H

#include <stdbool.h>

typedef enum {
    BUZ_CHIRP = 0, /* one short beep: key click, acknowledgement */
    BUZ_ALARM,     /* repeating on/off until buzzer_stop() */
    BUZ_ERROR,     /* three short beeps */
    BUZ_PATTERN_COUNT
} buzzer_pattern_t;

void buzzer_init(void);   /* pin low (silent); call before the scheduler */
void buzzer_play(buzzer_pattern_t p);
void buzzer_stop(void);
bool buzzer_is_busy(void); /* a pattern is playing */
const char *buzzer_pattern_name(buzzer_pattern_t p);

/* Direct control for the DIAG test; also stops any pattern. */
void buzzer_set(bool on);

#endif /* BUZZER_H */
