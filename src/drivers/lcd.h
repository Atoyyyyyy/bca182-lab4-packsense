/*
 * lcd.h: 1.3" 240x240 LCD, ST7789V3 controller, FSMC 8-bit 8080 bus.
 * Pins: include/board_pins.h (pinmap.md "LCD", sheet 8).
 *
 * Colours are RGB565. Drawing calls are not thread-safe: one task owns the
 * display (the CLI in M2, the UiTask from M5).
 */
#ifndef LCD_H
#define LCD_H

#include <stdbool.h>
#include <stdint.h>

#include "status.h"

#define LCD_WIDTH  240U
#define LCD_HEIGHT 240U

#define LCD_BLACK   0x0000U
#define LCD_WHITE   0xFFFFU
#define LCD_RED     0xF800U
#define LCD_GREEN   0x07E0U
#define LCD_BLUE    0x001FU
#define LCD_YELLOW  0xFFE0U

typedef struct {
    bool initialised;
    uint8_t id[3];       /* RDDID (0x04): ID1, ID2, ID3 as read, dummy byte dropped */
    uint8_t power_mode;  /* RDDPM (0x0A) */
    uint8_t madctl;      /* RDDMADCTL (0x0B) */
    uint8_t colmod;      /* RDDCOLMOD (0x0C) */
    bool backlight;
} lcd_info_t;

/* At boot, before the scheduler: backlight off and the panel held in reset,
   so nothing random shows until lcd_init() runs. */
void lcd_early_init(void);

/* Reset the panel, set up the FSMC, run the init sequence, clear to black and
   switch the backlight on. Uses vTaskDelay: call from a task. Returns
   ST_NOT_PRESENT if the ID bytes read back all 0x00 or all 0xFF. */
status_t lcd_init(void);
bool lcd_is_initialised(void);

/* Read the ID and status registers from the panel (fills `info`). */
void lcd_get_info(lcd_info_t *info);

void lcd_backlight(bool on);

/* Clipped to the screen. Colour is RGB565, sent high byte first. */
void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void lcd_fill(uint16_t color);

/* Bus check: write test values to MADCTL (0x36) and COLMOD (0x3A), read each
   back (0x0B / 0x0C), then restore the init values. Only the bits a register
   reads back are compared: MADCTL D7..D2, COLMOD D2..D0 (this panel returns
   0x05 after 0x65 is written). Together the values drive every data line
   D0..D7 both high and low. Returns the number of mismatches (0 = pass). */
#define LCD_BUS_TESTS 6U
typedef struct {
    uint8_t reg;    /* register written */
    uint8_t wrote;
    uint8_t expect; /* wrote & read-back mask */
    uint8_t got;
} lcd_bus_result_t;
unsigned lcd_bus_test(lcd_bus_result_t res[LCD_BUS_TESTS]);

/* Tearing-effect output on PD6: wait for `frames` + 1 rising edges and return
   the mean period in microseconds (0 = fewer than 2 edges before the
   timeout). `edges` gets the number of rising edges seen. Busy-waits for at
   most 100 ms + frames x 50 ms. */
uint32_t lcd_te_period_us(unsigned frames, unsigned *edges);

#endif /* LCD_H */
