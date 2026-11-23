/*
 * board_pins.h: RT-Spark (HW1.1) pins used by the firmware.
 *
 * Every value here is copied from docs/hardware/pinmap.md, which cites the
 * schematic sheet. Add a pin here only after it is in pinmap.md. If the board
 * disagrees with the schematic, fix pinmap.md first, then this file.
 */
#ifndef BOARD_PINS_H
#define BOARD_PINS_H

#include "stm32f4xx_hal.h"

/* Debug console: USART1 to the onboard ST-LINK/V2.1 virtual COM port
   (pinmap.md "Debug / console", sheets 3, 5, 12). */
#define CONSOLE_USART              USART1
#define CONSOLE_USART_IRQn         USART1_IRQn
#define CONSOLE_TX_PORT            GPIOA
#define CONSOLE_TX_PIN             GPIO_PIN_9
#define CONSOLE_RX_PORT            GPIOA
#define CONSOLE_RX_PIN             GPIO_PIN_10
#define CONSOLE_GPIO_AF            GPIO_AF7_USART1

/* Indicator LEDs: anode to +3V3 through 2 kOhm, so a LOW pin lights the LED
   (pinmap.md "Indicators and outputs", sheets 5, 6). */
#define LED_RED_PORT               GPIOF
#define LED_RED_PIN                GPIO_PIN_12
#define LED_BLUE_PORT              GPIOF
#define LED_BLUE_PIN               GPIO_PIN_11
#define LED_ON_LEVEL               GPIO_PIN_RESET
#define LED_OFF_LEVEL              GPIO_PIN_SET

#endif /* BOARD_PINS_H */
