#include "led.h"

#include "board_pins.h"

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
} led_pin_t;

static const led_pin_t s_leds[LED_COUNT] = {
    [LED_RED] = {LED_RED_PORT, LED_RED_PIN},
    [LED_BLUE] = {LED_BLUE_PORT, LED_BLUE_PIN},
};

void led_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOF_CLK_ENABLE();
    for (int i = 0; i < (int)LED_COUNT; i++) {
        /* Set the "off" level before the pin becomes an output: no glitch. */
        HAL_GPIO_WritePin(s_leds[i].port, s_leds[i].pin, LED_OFF_LEVEL);
        gpio.Pin = s_leds[i].pin;
        gpio.Mode = GPIO_MODE_OUTPUT_PP;
        gpio.Pull = GPIO_NOPULL;
        gpio.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(s_leds[i].port, &gpio);
    }
}

void led_set(led_id_t led, bool on)
{
    if (led < LED_COUNT) {
        HAL_GPIO_WritePin(s_leds[led].port, s_leds[led].pin, on ? LED_ON_LEVEL : LED_OFF_LEVEL);
    }
}

void led_toggle(led_id_t led)
{
    if (led < LED_COUNT) {
        HAL_GPIO_TogglePin(s_leds[led].port, s_leds[led].pin);
    }
}

bool led_is_on(led_id_t led)
{
    if (led >= LED_COUNT) {
        return false;
    }
    /* Read the output data register: what we drive, not the pin level. */
    bool high = (s_leds[led].port->ODR & s_leds[led].pin) != 0U;
    return (LED_ON_LEVEL == GPIO_PIN_SET) ? high : !high;
}
