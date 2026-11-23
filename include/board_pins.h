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

/* Active (self-oscillating) buzzer BUZ1 through an S8050 NPN: HIGH = sound
   (pinmap.md "Indicators and outputs", sheets 5, 6). */
#define BUZZER_PORT                GPIOB
#define BUZZER_PIN                 GPIO_PIN_0
#define BUZZER_ON_LEVEL            GPIO_PIN_SET
#define BUZZER_OFF_LEVEL           GPIO_PIN_RESET

/* Arrow keys SW2-SW5: switch to GND, no external pull-up drawn, so internal
   pull-up and pressed = LOW. Each key has its own EXTI line
   (pinmap.md "User input", sheets 5, 6). */
#define KEYS_PORT                  GPIOC
#define KEY_UP_PIN                 GPIO_PIN_5 /* EXTI9_5 */
#define KEY_DOWN_PIN               GPIO_PIN_1 /* EXTI1 */
#define KEY_LEFT_PIN               GPIO_PIN_0 /* EXTI0 */
#define KEY_RIGHT_PIN              GPIO_PIN_4 /* EXTI4 */

/* Hardware I2C2: ICM-20608-G 0x68, AP3216C 0x1E, ES8388 0x10; 5.1 kOhm
   pull-ups R1/R2 (pinmap.md "I2C buses and devices", sheets 5, 6, 9).
   PF0/PF1 carry I2C2 on alternate function 4 (STM32F407 datasheet AF table). */
#define I2C_MAIN_INSTANCE          I2C2
#define I2C_MAIN_EV_IRQn           I2C2_EV_IRQn
#define I2C_MAIN_ER_IRQn           I2C2_ER_IRQn
#define I2C_MAIN_SCL_PORT          GPIOF
#define I2C_MAIN_SCL_PIN           GPIO_PIN_1
#define I2C_MAIN_SDA_PORT          GPIOF
#define I2C_MAIN_SDA_PIN           GPIO_PIN_0
#define I2C_MAIN_GPIO_AF           GPIO_AF4_I2C2

/* Bit-banged I2C ("SOFT_I2C"): AHT21 0x38; 5.1 kOhm pull-ups R6/R7
   (pinmap.md "I2C buses and devices", sheets 5, 6). */
#define I2C_SOFT_SCL_PORT          GPIOE
#define I2C_SOFT_SCL_PIN           GPIO_PIN_0
#define I2C_SOFT_SDA_PORT          GPIOE
#define I2C_SOFT_SDA_PIN           GPIO_PIN_1

/* 7-bit I2C addresses (pinmap.md; ICM AD0 is tied to GND). */
#define I2C_ADDR_ICM20608          0x68U
#define I2C_ADDR_AP3216C           0x1EU
#define I2C_ADDR_ES8388            0x10U
#define I2C_ADDR_AHT               0x38U

/* Sensor interrupt lines (not used yet: sensors are polled). */
#define ICM_INT_PORT               GPIOG
#define ICM_INT_PIN                GPIO_PIN_12
#define APS_INT_PORT               GPIOG
#define APS_INT_PIN                GPIO_PIN_13

#endif /* BOARD_PINS_H */
