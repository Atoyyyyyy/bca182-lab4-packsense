#include "buzzer.h"

#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "timers.h"

#include "board_pins.h"

/* Each pattern is a list of durations in ms: on, off, on, off, ... ended by 0.
   A repeating pattern starts again from the top after the last step. */
typedef struct {
    const char *name;
    const uint16_t *steps;
    bool repeat;
} pattern_t;

static const uint16_t s_chirp[] = {60U, 0U};
static const uint16_t s_alarm[] = {200U, 200U, 0U};
static const uint16_t s_error[] = {80U, 80U, 80U, 80U, 80U, 0U};

static const pattern_t s_patterns[BUZ_PATTERN_COUNT] = {
    [BUZ_CHIRP] = {"chirp", s_chirp, false},
    [BUZ_ALARM] = {"alarm", s_alarm, true},
    [BUZ_ERROR] = {"error", s_error, false},
};

static StaticTimer_t s_timer_buf;
static TimerHandle_t s_timer;

/* Owned by the timer service task once a pattern starts; written by callers
   only inside a critical section while the timer is stopped. */
static const pattern_t *volatile s_cur;
static volatile size_t s_step;

static void pin_write(bool on)
{
    HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, on ? BUZZER_ON_LEVEL : BUZZER_OFF_LEVEL);
}

/* Runs in the timer service task: apply step s_step, arm the timer for its
   duration. Even steps are "on", odd steps "off". */
static void step_cb(TimerHandle_t t)
{
    (void)t;
    const pattern_t *p;
    size_t i;

    taskENTER_CRITICAL();
    p = s_cur;
    i = s_step;
    if (p != NULL && p->steps[i] == 0U && p->repeat) {
        i = 0U;
    }
    if (p != NULL && p->steps[i] != 0U) {
        s_step = i + 1U;
    } else {
        s_cur = NULL;
        p = NULL;
    }
    taskEXIT_CRITICAL();

    if (p == NULL) {
        pin_write(false);
        return;
    }
    pin_write((i % 2U) == 0U);
    (void)xTimerChangePeriod(s_timer, pdMS_TO_TICKS(p->steps[i]), 0);
}

void buzzer_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    pin_write(false); /* level first, then output: no click at boot */
    gpio.Pin = BUZZER_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BUZZER_PORT, &gpio);

    s_timer = xTimerCreateStatic("buzzer", 1, pdFALSE, NULL, step_cb, &s_timer_buf);
    configASSERT(s_timer != NULL);
}

void buzzer_play(buzzer_pattern_t p)
{
    if (p >= BUZ_PATTERN_COUNT) {
        return;
    }
    (void)xTimerStop(s_timer, portMAX_DELAY);
    taskENTER_CRITICAL();
    s_cur = &s_patterns[p];
    s_step = 0U;
    taskEXIT_CRITICAL();
    /* Fire on the next tick; the callback plays step 0. */
    (void)xTimerChangePeriod(s_timer, 1, portMAX_DELAY);
}

void buzzer_stop(void)
{
    (void)xTimerStop(s_timer, portMAX_DELAY);
    taskENTER_CRITICAL();
    s_cur = NULL;
    taskEXIT_CRITICAL();
    /* The stop command is queued; a callback already running sees s_cur ==
       NULL on its next run. Silence the pin now as well. */
    pin_write(false);
}

bool buzzer_is_busy(void)
{
    return s_cur != NULL;
}

const char *buzzer_pattern_name(buzzer_pattern_t p)
{
    return (p < BUZ_PATTERN_COUNT) ? s_patterns[p].name : "?";
}

void buzzer_set(bool on)
{
    buzzer_stop();
    pin_write(on);
}
