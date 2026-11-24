/*
 * gfx.h: text on the 240 x 240 LCD with the generated DejaVu Sans Mono
 * bitmap fonts (font_data.h). Small = 8 x 16 (30 columns x 15 rows),
 * large = 12 x 24, both scalable (x2 large = 24 x 48 for the state name).
 * Call from one task only (the UI owns the LCD).
 */
#ifndef GFX_H
#define GFX_H

#include <stdint.h>

#include "drivers/lcd.h"

typedef enum { FONT_SMALL = 0, FONT_LARGE } gfx_font_t;

#define GFX_RGB(r, g, b) ((uint16_t)((((r) & 0xF8U) << 8) | (((g) & 0xFCU) << 3) | ((b) >> 3)))
#define GFX_GREY   GFX_RGB(120, 120, 120)
#define GFX_ORANGE GFX_RGB(255, 140, 0)
#define GFX_CYAN   GFX_RGB(0, 200, 220)
#define GFX_MAGENTA GFX_RGB(230, 0, 230)
#define GFX_DKBLUE GFX_RGB(0, 0, 90)

uint16_t gfx_char_w(gfx_font_t f, uint8_t scale);
uint16_t gfx_char_h(gfx_font_t f, uint8_t scale);

/* Draw s at (x, y); characters that would not fit are dropped. Returns the x
   after the last character. */
uint16_t gfx_text(uint16_t x, uint16_t y, const char *s, gfx_font_t f, uint8_t scale, uint16_t fg, uint16_t bg);

/* Text padded with bg to `cols` characters (clears what was there before). */
void gfx_text_pad(uint16_t x, uint16_t y, const char *s, unsigned cols, gfx_font_t f, uint8_t scale, uint16_t fg,
                  uint16_t bg);

/* Centred horizontally on the panel, full-width background bar. */
void gfx_text_center(uint16_t y, const char *s, gfx_font_t f, uint8_t scale, uint16_t fg, uint16_t bg);

#endif /* GFX_H */
