/*
 * gfx.c: bitmap-font text on the LCD (see gfx.h).
 */
#include "gfx.h"

#include <string.h>

#include "font_data.h"

static const uint8_t *glyph(gfx_font_t f, char c, uint16_t *w, uint16_t *h, uint16_t *stride)
{
    if ((uint8_t)c < FONT_FIRST || (uint8_t)c > FONT_LAST) {
        c = '?';
    }
    const unsigned i = (uint8_t)c - FONT_FIRST;
    if (f == FONT_LARGE) {
        *w = FONT12X24_W;
        *h = FONT12X24_H;
        *stride = (FONT12X24_W + 7) / 8;
        return &font12x24[i * FONT12X24_H * *stride];
    }
    *w = FONT8X16_W;
    *h = FONT8X16_H;
    *stride = (FONT8X16_W + 7) / 8;
    return &font8x16[i * FONT8X16_H * *stride];
}

uint16_t gfx_char_w(gfx_font_t f, uint8_t scale)
{
    return (uint16_t)((f == FONT_LARGE ? FONT12X24_W : FONT8X16_W) * scale);
}

uint16_t gfx_char_h(gfx_font_t f, uint8_t scale)
{
    return (uint16_t)((f == FONT_LARGE ? FONT12X24_H : FONT8X16_H) * scale);
}

uint16_t gfx_text(uint16_t x, uint16_t y, const char *s, gfx_font_t f, uint8_t scale, uint16_t fg, uint16_t bg)
{
    const uint16_t cw = gfx_char_w(f, scale);
    for (; *s && x + cw <= LCD_WIDTH; s++) {
        uint16_t w, h, stride;
        const uint8_t *g = glyph(f, *s, &w, &h, &stride);
        lcd_draw_mono(x, y, w, h, g, stride, fg, bg, scale);
        x = (uint16_t)(x + cw);
    }
    return x;
}

void gfx_text_pad(uint16_t x, uint16_t y, const char *s, unsigned cols, gfx_font_t f, uint8_t scale, uint16_t fg,
                  uint16_t bg)
{
    const uint16_t end = gfx_text(x, y, s, f, scale, fg, bg);
    const uint16_t want = (uint16_t)(x + cols * gfx_char_w(f, scale));
    if (want > end) {
        lcd_fill_rect(end, y, (uint16_t)((want > LCD_WIDTH ? LCD_WIDTH : want) - end), gfx_char_h(f, scale), bg);
    }
}

void gfx_text_center(uint16_t y, const char *s, gfx_font_t f, uint8_t scale, uint16_t fg, uint16_t bg)
{
    const uint16_t cw = gfx_char_w(f, scale);
    size_t n = strlen(s);
    if (n * cw > LCD_WIDTH) {
        n = LCD_WIDTH / cw;
    }
    const uint16_t x = (uint16_t)((LCD_WIDTH - n * cw) / 2U);
    const uint16_t h = gfx_char_h(f, scale);
    lcd_fill_rect(0, y, x, h, bg);
    const uint16_t end = gfx_text(x, y, s, f, scale, fg, bg);
    if (end < LCD_WIDTH) {
        lcd_fill_rect(end, y, (uint16_t)(LCD_WIDTH - end), h, bg);
    }
}
