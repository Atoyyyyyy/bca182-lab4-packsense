/*
 * lcd_cmds.c: console commands for the ST7789V3 LCD.
 *
 * The first `lcd ...` command initialises the panel. Output lines start with
 * [lcd] and t=<ms since boot>, like the other DIAG commands. What the screen
 * shows can only be checked by a person: tools/capture_m2.py lcd asks.
 */
#include "lcd_cmds.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "app/cli.h"
#include "board_pins.h" /* stm32f4xx_hal.h: DWT, SystemCoreClock */
#include "drivers/console_uart.h"
#include "drivers/lcd.h"

#define T_NOW() ((unsigned long)xTaskGetTickCount())

static bool lcd_ready(bool force)
{
    if (lcd_is_initialised() && !force) {
        return true;
    }
    const TickType_t t0 = xTaskGetTickCount();
    status_t st = lcd_init();
    lcd_info_t info;
    lcd_get_info(&info);
    console_printf("[lcd] t=%lu init %s in %lu ms\r\n", T_NOW(), status_str(st),
                   (unsigned long)(xTaskGetTickCount() - t0));
    if (st == ST_NOT_PRESENT) {
        console_puts("[lcd] ID read back all 0x00 or all 0xFF: no panel answering on the FSMC bus\r\n");
    }
    return st == ST_OK;
}

static void print_info(void)
{
    lcd_info_t i;
    lcd_get_info(&i);
    console_printf("[lcd] t=%lu id=%02X %02X %02X power_mode=0x%02X madctl=0x%02X colmod=0x%02X backlight=%s\r\n",
                   T_NOW(), i.id[0], i.id[1], i.id[2], i.power_mode, i.madctl, i.colmod,
                   i.backlight ? "on" : "off");
    /* RDDPM bits (MIPI DCS get_power_mode). */
    const uint8_t p = i.power_mode;
    console_printf("[lcd] power_mode: booster=%u idle=%u partial=%u sleep_out=%u normal=%u display_on=%u\r\n",
                   (p >> 7) & 1U, (p >> 6) & 1U, (p >> 5) & 1U, (p >> 4) & 1U, (p >> 3) & 1U, (p >> 2) & 1U);
}

/* "lcd": init on first use, then ID + status. "lcd init": run init again. */
static int cmd_lcd(int argc, char **argv)
{
    if (argc > 1) {
        return 1;
    }
    bool force = false;
    if (argc == 1) {
        if (strcmp(argv[0], "init") != 0) {
            return 1;
        }
        force = true;
    }
    if (!lcd_ready(force)) {
        return 0;
    }
    print_info();
    return 0;
}

/* "lcd bl on|off" */
static int cmd_lcd_bl(int argc, char **argv)
{
    if (argc != 1 || (strcmp(argv[0], "on") != 0 && strcmp(argv[0], "off") != 0)) {
        return 1;
    }
    if (!lcd_ready(false)) {
        return 0;
    }
    const bool on = strcmp(argv[0], "on") == 0;
    lcd_backlight(on);
    console_printf("[lcd] t=%lu backlight %s\r\n", T_NOW(), on ? "on" : "off");
    return 0;
}

typedef struct {
    const char *name;
    uint16_t rgb565;
} named_color_t;

static const named_color_t s_colors[] = {
    {"black", LCD_BLACK}, {"white", LCD_WHITE}, {"red", LCD_RED},
    {"green", LCD_GREEN}, {"blue", LCD_BLUE},   {"yellow", LCD_YELLOW},
};

/* "lcd fill <colour>": whole screen, prints how long the 115200-byte write took. */
static int cmd_lcd_fill(int argc, char **argv)
{
    if (argc != 1) {
        return 1;
    }
    for (unsigned i = 0; i < sizeof s_colors / sizeof s_colors[0]; i++) {
        if (strcmp(argv[0], s_colors[i].name) != 0) {
            continue;
        }
        if (!lcd_ready(false)) {
            return 0;
        }
        const uint32_t c0 = DWT->CYCCNT;
        lcd_fill(s_colors[i].rgb565);
        const uint32_t us = (DWT->CYCCNT - c0) / (SystemCoreClock / 1000000U);
        console_printf("[lcd] t=%lu fill %s rgb565=0x%04X %lux%lu took %lu us\r\n", T_NOW(), s_colors[i].name,
                       s_colors[i].rgb565, (unsigned long)LCD_WIDTH, (unsigned long)LCD_HEIGHT,
                       (unsigned long)us);
        return 0;
    }
    return 1;
}

/* "lcd test": black screen, 2 px white frame on all four edges (shows whether
   every row and column is visible), a 40x40 square in each corner (TL red,
   TR green, BL blue, BR yellow: shows the orientation), and three bars in
   the middle (red, green, blue, left to right). */
static int cmd_lcd_test(int argc, char **argv)
{
    (void)argv;
    if (argc != 0) {
        return 1;
    }
    if (!lcd_ready(false)) {
        return 0;
    }
    const uint16_t W = LCD_WIDTH, H = LCD_HEIGHT, B = 2U, S = 40U;
    lcd_fill(LCD_BLACK);
    lcd_fill_rect(0, 0, W, B, LCD_WHITE);
    lcd_fill_rect(0, (uint16_t)(H - B), W, B, LCD_WHITE);
    lcd_fill_rect(0, 0, B, H, LCD_WHITE);
    lcd_fill_rect((uint16_t)(W - B), 0, B, H, LCD_WHITE);
    lcd_fill_rect(B, B, S, S, LCD_RED);
    lcd_fill_rect((uint16_t)(W - B - S), B, S, S, LCD_GREEN);
    lcd_fill_rect(B, (uint16_t)(H - B - S), S, S, LCD_BLUE);
    lcd_fill_rect((uint16_t)(W - B - S), (uint16_t)(H - B - S), S, S, LCD_YELLOW);
    const uint16_t bar_w = 40U, bar_h = 60U, gap = 10U;
    const uint16_t x0 = (uint16_t)((W - 3U * bar_w - 2U * gap) / 2U);
    const uint16_t y0 = (uint16_t)((H - bar_h) / 2U);
    lcd_fill_rect(x0, y0, bar_w, bar_h, LCD_RED);
    lcd_fill_rect((uint16_t)(x0 + bar_w + gap), y0, bar_w, bar_h, LCD_GREEN);
    lcd_fill_rect((uint16_t)(x0 + 2U * (bar_w + gap)), y0, bar_w, bar_h, LCD_BLUE);
    console_printf("[lcd] t=%lu test pattern: white frame; corners TL red, TR green, BL blue, BR yellow; "
                   "middle bars red green blue\r\n", T_NOW());
    return 0;
}

/* "lcd bus": data-line read-back check. */
static int cmd_lcd_bus(int argc, char **argv)
{
    (void)argv;
    if (argc != 0) {
        return 1;
    }
    if (!lcd_ready(false)) {
        return 0;
    }
    lcd_bus_result_t r[LCD_BUS_TESTS];
    const unsigned bad = lcd_bus_test(r);
    for (unsigned i = 0; i < LCD_BUS_TESTS; i++) {
        console_printf("[lcd] bus reg=0x%02X wrote=0x%02X expect=0x%02X read=0x%02X %s\r\n", r[i].reg, r[i].wrote,
                       r[i].expect, r[i].got, r[i].expect == r[i].got ? "ok" : "MISMATCH");
    }
    console_printf("[lcd] t=%lu bus test %s (%u of %u mismatched)\r\n", T_NOW(), bad == 0U ? "PASS" : "FAIL",
                   bad, (unsigned)LCD_BUS_TESTS);
    return 0;
}

/* "lcd te [frames]": tearing-effect period on PD6 (the panel's frame rate). */
static int cmd_lcd_te(int argc, char **argv)
{
    unsigned long frames = 30UL;
    if (argc > 1 || (argc == 1 && !cli_arg_u32(argv[0], 2UL, 120UL, &frames))) {
        return 1;
    }
    if (!lcd_ready(false)) {
        return 0;
    }
    unsigned edges = 0U;
    const uint32_t us = lcd_te_period_us((unsigned)frames, &edges);
    if (us == 0U) {
        console_printf("[lcd] t=%lu te: %u rising edges on PD6 (none or one): no TE signal\r\n", T_NOW(), edges);
        return 0;
    }
    console_printf("[lcd] t=%lu te: %u rising edges, period %lu us = %lu.%02lu Hz\r\n", T_NOW(), edges,
                   (unsigned long)us, 1000000UL / us, (100000000UL / us) % 100UL);
    return 0;
}

static const cli_cmd_t s_cmds[] = {
    {"lcd", "[init]", "LCD ID and status (inits the panel on first use)", cmd_lcd},
    {"lcd bl", "on|off", "LCD backlight", cmd_lcd_bl},
    {"lcd fill", "black|white|red|green|blue|yellow", "fill the screen, print the time taken", cmd_lcd_fill},
    {"lcd test", "", "frame + corner squares + colour bars (orientation check)", cmd_lcd_test},
    {"lcd bus", "", "FSMC data-line write/read-back check", cmd_lcd_bus},
    {"lcd te", "[frames]", "frame rate from the tearing-effect pin (default 30 frames)", cmd_lcd_te},
};

void lcd_cmds_register(void)
{
    cli_register(s_cmds, sizeof s_cmds / sizeof s_cmds[0]);
}
