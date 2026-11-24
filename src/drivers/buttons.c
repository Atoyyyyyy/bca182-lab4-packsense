#include "buttons.h"

#include <stddef.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "app_config.h"
#include "board_pins.h"

typedef struct {
    uint8_t key;
    TickType_t tick;
} raw_edge_t;

static const uint16_t s_pins[BTN_COUNT] = {
    [BTN_UP] = KEY_UP_PIN,
    [BTN_DOWN] = KEY_DOWN_PIN,
    [BTN_LEFT] = KEY_LEFT_PIN,
    [BTN_RIGHT] = KEY_RIGHT_PIN,
};
static const char *const s_names[BTN_COUNT] = {"UP", "DOWN", "LEFT", "RIGHT"};

#define KEYS_PIN_MASK (KEY_UP_PIN | KEY_DOWN_PIN | KEY_LEFT_PIN | KEY_RIGHT_PIN)

static StaticQueue_t s_q_buf;
static uint8_t s_q_storage[BUTTON_RAW_QUEUE_LEN * sizeof(raw_edge_t)];
static QueueHandle_t s_q;

/* ISR-written counters (32-bit stores are atomic on the M4). */
static volatile uint32_t s_raw_edges[BTN_COUNT];
static volatile uint32_t s_dropped[BTN_COUNT];

/* Consumer-task state. */
static bool s_stable[BTN_COUNT];       /* true = pressed */
static TickType_t s_press_tick[BTN_COUNT];
static uint32_t s_edges_since[BTN_COUNT]; /* raw edges since the last stable change */
static TickType_t s_first_edge[BTN_COUNT]; /* ISR time of the first of those edges */
static uint32_t s_presses[BTN_COUNT];
static uint32_t s_long[BTN_COUNT];
static button_event_t s_pending[BTN_COUNT]; /* events found in one settle pass */
static size_t s_n_pending;

static bool pin_pressed(button_id_t k)
{
    return HAL_GPIO_ReadPin(KEYS_PORT, s_pins[k]) == GPIO_PIN_RESET;
}

void buttons_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_SYSCFG_CLK_ENABLE(); /* EXTI line -> port C mapping */

    s_q = xQueueCreateStatic(BUTTON_RAW_QUEUE_LEN, sizeof(raw_edge_t), s_q_storage, &s_q_buf);
    configASSERT(s_q != NULL);

    gpio.Pin = KEYS_PIN_MASK;
    gpio.Mode = GPIO_MODE_IT_RISING_FALLING;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(KEYS_PORT, &gpio);

    for (int k = 0; k < (int)BTN_COUNT; k++) {
        s_stable[k] = pin_pressed((button_id_t)k);
    }
    __HAL_GPIO_EXTI_CLEAR_IT(KEYS_PIN_MASK);

    static const IRQn_Type irqs[] = {EXTI0_IRQn, EXTI1_IRQn, EXTI4_IRQn, EXTI9_5_IRQn};
    for (size_t i = 0; i < sizeof irqs / sizeof irqs[0]; i++) {
        HAL_NVIC_SetPriority(irqs[i], IRQ_PRIO_BUTTONS, 0);
        HAL_NVIC_EnableIRQ(irqs[i]);
    }
}

void buttons_exti_irq(void)
{
    BaseType_t woken = pdFALSE;
    const uint32_t pending = EXTI->PR & KEYS_PIN_MASK;

    if (pending == 0U) {
        return; /* another source on a shared line (EXTI9_5) */
    }
    EXTI->PR = pending; /* write 1 to clear */
    const TickType_t now = xTaskGetTickCountFromISR();
    for (int k = 0; k < (int)BTN_COUNT; k++) {
        if ((pending & s_pins[k]) == 0U) {
            continue;
        }
        s_raw_edges[k]++;
        raw_edge_t e = {(uint8_t)k, now};
        if (xQueueSendFromISR(s_q, &e, &woken) != pdPASS) {
            s_dropped[k]++;
        }
    }
    portYIELD_FROM_ISR(woken);
}

/* Wait until no edge has arrived for BUTTON_DEBOUNCE_MS, then compare every
   key's level with its stable state and queue the changes in s_pending. */
static void settle(const raw_edge_t *first)
{
    raw_edge_t e = *first;
    do {
        if (e.key < BTN_COUNT) {
            if (s_edges_since[e.key] == 0U) {
                s_first_edge[e.key] = e.tick;
            }
            s_edges_since[e.key]++;
        }
    } while (xQueueReceive(s_q, &e, pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS)) == pdPASS);

    const TickType_t now = xTaskGetTickCount();
    for (int k = 0; k < (int)BTN_COUNT; k++) {
        const bool level = pin_pressed((button_id_t)k);
        if (level == s_stable[k]) {
            continue; /* bounced back, or an edge pair that cancelled out */
        }
        s_stable[k] = level;
        button_event_t *ev = &s_pending[s_n_pending++];
        ev->key = (button_id_t)k;
        ev->pressed = level;
        ev->t_ms = (uint32_t)now;
        ev->t_edge_ms = s_edges_since[k] ? (uint32_t)s_first_edge[k] : (uint32_t)now;
        ev->edges = s_edges_since[k];
        s_edges_since[k] = 0U;
        if (level) {
            s_press_tick[k] = now;
            ev->held_ms = 0U;
            ev->long_press = false;
            s_presses[k]++;
        } else {
            ev->held_ms = (uint32_t)(now - s_press_tick[k]);
            ev->long_press = ev->held_ms >= BUTTON_LONG_PRESS_MS;
            if (ev->long_press) {
                s_long[k]++;
            }
        }
    }
}

bool buttons_get_event(button_event_t *ev, TickType_t wait)
{
    if (s_n_pending == 0U) {
        raw_edge_t e;
        if (xQueueReceive(s_q, &e, wait) != pdPASS) {
            return false;
        }
        settle(&e);
        if (s_n_pending == 0U) {
            return false; /* pure bounce, nothing changed */
        }
    }
    *ev = s_pending[0];
    for (size_t i = 1; i < s_n_pending; i++) {
        s_pending[i - 1U] = s_pending[i];
    }
    s_n_pending--;
    return true;
}

void buttons_resync(void)
{
    (void)xQueueReset(s_q);
    s_n_pending = 0U;
    const TickType_t now = xTaskGetTickCount();
    for (int k = 0; k < (int)BTN_COUNT; k++) {
        s_stable[k] = pin_pressed((button_id_t)k);
        s_edges_since[k] = 0U;
        s_press_tick[k] = now;
    }
}

void buttons_get_stats(button_id_t key, button_stats_t *out)
{
    if (key >= BTN_COUNT || out == NULL) {
        return;
    }
    out->raw_edges = s_raw_edges[key];
    out->presses = s_presses[key];
    out->long_presses = s_long[key];
    out->dropped = s_dropped[key];
    out->pressed = s_stable[key];
}

const char *button_name(button_id_t key)
{
    return (key < BTN_COUNT) ? s_names[key] : "?";
}
