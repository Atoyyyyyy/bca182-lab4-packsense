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

/* LCD: ST7789V3 240x240 on FSMC bank 1 region 3 (NE3), 8-bit 8080 bus. D/C
   is FSMC_A18, so with an 8-bit bus the data register is base + (1 << 18)
   (pinmap.md "LCD", sheet 8). All FSMC pins use alternate function 12. */
#define LCD_FSMC_BANK_BASE         0x68000000UL
#define LCD_FSMC_DC_ADDR_BIT       18U
#define LCD_FSMC_GPIO_AF           GPIO_AF12_FSMC
/* port D: D2 PD0, D3 PD1, NOE PD4, NWE PD5, A18 PD13, D0 PD14, D1 PD15 */
#define LCD_FSMC_PD_PINS           (GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5 | \
                                    GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15)
/* port E: D4 PE7, D5 PE8, D6 PE9, D7 PE10 */
#define LCD_FSMC_PE_PINS           (GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10)
/* port G: NE3 PG10 */
#define LCD_FSMC_PG_PINS           GPIO_PIN_10
#define LCD_RST_PORT               GPIOD
#define LCD_RST_PIN                GPIO_PIN_3  /* low = reset */
#define LCD_TE_PORT                GPIOD
#define LCD_TE_PIN                 GPIO_PIN_6  /* tearing-effect output of the panel */
#define LCD_BL_PORT                GPIOF
#define LCD_BL_PIN                 GPIO_PIN_9  /* S8050 low-side switch: HIGH = backlight on */
#define LCD_BL_ON_LEVEL            GPIO_PIN_SET
#define LCD_BL_OFF_LEVEL           GPIO_PIN_RESET

/* LED matrix: 19 x SK6805 serial RGB behind a 74LVC1G125 buffer whose /OE
   (EN) has a 10 kOhm pull-up, so EN LOW enables the data line
   (pinmap.md "Indicators and outputs", sheets 5, 6). PA7 is also PMOD1 MOSI.
   PA7 = TIM3_CH2 on AF2 (STM32F407 datasheet AF table); the waveform comes
   from TIM3 PWM fed by DMA1 stream 5 channel 5 (TIM3_CH2 request), as in the
   board vendor's driver. Note: the IR transmitter PB1 is TIM3_CH4. */
#define LEDMX_DATA_PORT            GPIOA
#define LEDMX_DATA_PIN             GPIO_PIN_7
#define LEDMX_DATA_AF              GPIO_AF2_TIM3
#define LEDMX_EN_PORT              GPIOF
#define LEDMX_EN_PIN               GPIO_PIN_2
#define LEDMX_EN_ON_LEVEL          GPIO_PIN_RESET
#define LEDMX_EN_OFF_LEVEL         GPIO_PIN_SET
#define LEDMX_COUNT                19U

/* microSD, SDIO 4-bit: D0..D3 PC8..PC11, CK PC12, CMD PD2, all AF12;
   card detect GPIO_CARD_DETECT on PF3 (pinmap.md "Storage", sheets 5, 7).
   The card-detect polarity is not confirmed yet (no card in our kit so far),
   so the driver reports the raw pin level and relies on the SDIO init. */
#define SD_DATA_PORT               GPIOC
#define SD_DATA_PINS               (GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11)
#define SD_CK_PORT                 GPIOC
#define SD_CK_PIN                  GPIO_PIN_12
#define SD_CMD_PORT                GPIOD
#define SD_CMD_PIN                 GPIO_PIN_2
#define SD_GPIO_AF                 GPIO_AF12_SDIO
#define SD_CD_PORT                 GPIOF
#define SD_CD_PIN                  GPIO_PIN_3

/* SPI2: W25Q SPI flash (CS0 = PB12) and the RW007 Wi-Fi module (CS1 = PF10)
   share SCK PB13, MISO PC2, MOSI PC3 (pinmap.md "Storage", "Wireless",
   sheets 5, 7, 10). AF5 = SPI2 on all three (STM32F407 datasheet AF table).
   Both chip selects are driven HIGH (deselected) at boot, so the RW007 can
   never answer while the flash is being used. */
#define SPI2_SCK_PORT              GPIOB
#define SPI2_SCK_PIN               GPIO_PIN_13
#define SPI2_MISO_PORT             GPIOC
#define SPI2_MISO_PIN              GPIO_PIN_2
#define SPI2_MOSI_PORT             GPIOC
#define SPI2_MOSI_PIN              GPIO_PIN_3
#define SPI2_GPIO_AF               GPIO_AF5_SPI2
#define FLASH_CS_PORT              GPIOB
#define FLASH_CS_PIN               GPIO_PIN_12
#define WIFI_CS_PORT               GPIOF
#define WIFI_CS_PIN                GPIO_PIN_10
/* RW007 control lines (pinmap.md "Wireless", sheet 10): RST PG15 (LOW =
   reset), INT/BUSY PG11 (module output; LOW while booting, then a falling
   edge each time it has finished a transfer stage or has data). PG11 uses
   EXTI11 (EXTI15_10 vector, no other user). */
#define WIFI_RST_PORT              GPIOG
#define WIFI_RST_PIN               GPIO_PIN_15
#define WIFI_INT_PORT              GPIOG
#define WIFI_INT_PIN               GPIO_PIN_11

/* IR: transmitter LED on PB1 through an S8050 NPN (HIGH = LED on), receiver
   IRM-H638T on PF8, demodulated output active LOW (pinmap.md "Indicators and
   outputs", sheets 5, 6). The 38 kHz carrier comes from TIM8_CH3N on PB1
   (AF3, STM32F407 datasheet AF table) rather than TIM3_CH4, because TIM3
   runs the LED matrix. PF8 uses EXTI8 (EXTI9_5 vector, shared with key UP). */
#define IR_TX_PORT                 GPIOB
#define IR_TX_PIN                  GPIO_PIN_1
#define IR_TX_AF                   GPIO_AF3_TIM8
#define IR_RX_PORT                 GPIOF
#define IR_RX_PIN                  GPIO_PIN_8

/* Sensor interrupt lines (not used yet: sensors are polled). */
#define ICM_INT_PORT               GPIOG
#define ICM_INT_PIN                GPIO_PIN_12
#define APS_INT_PORT               GPIOG
#define APS_INT_PIN                GPIO_PIN_13

#endif /* BOARD_PINS_H */
