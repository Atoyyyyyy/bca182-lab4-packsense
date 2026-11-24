/*
 * rtc_cmds.c: console commands for the RTC (PRD 8.6 `rtc set`).
 *
 *   rtc                               init on first use, print clock source + time
 *   rtc set YYYY-MM-DD hh:mm:ss       set the calendar
 *   rtc check [seconds]               RTC rate vs the CPU clock (HSE crystal), in ppm
 */
#include "rtc_cmds.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "app/cli.h"
#include "drivers/console_uart.h"
#include "drivers/rtc.h"

#define T_NOW() ((unsigned long)xTaskGetTickCount())

static bool rtc_ready(void)
{
    if (rtc_is_initialised()) {
        return true;
    }
    const TickType_t t0 = xTaskGetTickCount();
    status_t st = rtc_init();
    rtc_info_t i;
    rtc_get_info(&i);
    console_printf("[rtc] t=%lu init %s in %lu ms: source=%s kept_running=%s lse_start_ms=%lu\r\n", T_NOW(),
                   status_str(st), (unsigned long)(xTaskGetTickCount() - t0), rtc_source_name(i.source),
                   i.kept_running ? "yes" : "no", (unsigned long)i.lse_start_ms);
    if (st == ST_OK && i.source == RTC_SRC_LSI) {
        console_puts("[rtc] WARNING: LSE did not start, running on the LSI RC oscillator (inaccurate)\r\n");
    }
    return st == ST_OK;
}

static void print_time(void)
{
    rtc_datetime_t dt;
    rtc_info_t i;
    rtc_get_info(&i);
    status_t st = rtc_get(&dt);
    if (st != ST_OK) {
        console_printf("[rtc] t=%lu read %s\r\n", T_NOW(), status_str(st));
        return;
    }
    console_printf("[rtc] t=%lu %04u-%02u-%02u %02u:%02u:%02u.%03u source=%s time_valid=%s\r\n", T_NOW(),
                   dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second, dt.ms, rtc_source_name(i.source),
                   i.time_valid ? "yes" : "no (not set since power-up)");
}

static int cmd_rtc(int argc, char **argv)
{
    (void)argv;
    if (argc != 0) {
        return 1;
    }
    if (rtc_ready()) {
        print_time();
    }
    return 0;
}

/* Parse exactly `n` decimal digits from *p, advance it. */
static bool digits(const char **p, unsigned n, unsigned *out)
{
    unsigned v = 0U;
    for (unsigned i = 0; i < n; i++) {
        const char c = (*p)[i];
        if (c < '0' || c > '9') {
            return false;
        }
        v = v * 10U + (unsigned)(c - '0');
    }
    *p += n;
    *out = v;
    return true;
}

static bool sep(const char **p, char c)
{
    if (**p != c) {
        return false;
    }
    (*p)++;
    return true;
}

static int cmd_rtc_set(int argc, char **argv)
{
    if (argc != 2) {
        return 1;
    }
    unsigned y, mo, d, h, mi, s;
    const char *p = argv[0];
    const char *q = argv[1];
    if (!digits(&p, 4, &y) || !sep(&p, '-') || !digits(&p, 2, &mo) || !sep(&p, '-') || !digits(&p, 2, &d) ||
        *p != '\0' || !digits(&q, 2, &h) || !sep(&q, ':') || !digits(&q, 2, &mi) || !sep(&q, ':') ||
        !digits(&q, 2, &s) || *q != '\0') {
        console_puts("[rtc] format: rtc set YYYY-MM-DD hh:mm:ss\r\n");
        return 0;
    }
    if (!rtc_ready()) {
        return 0;
    }
    const rtc_datetime_t dt = {(uint16_t)y, (uint8_t)mo, (uint8_t)d, (uint8_t)h, (uint8_t)mi, (uint8_t)s, 0U};
    status_t st = rtc_set(&dt);
    console_printf("[rtc] t=%lu set %s\r\n", T_NOW(), status_str(st));
    print_time();
    return 0;
}

static int cmd_rtc_check(int argc, char **argv)
{
    unsigned long secs = 20UL;
    if (argc > 1 || (argc == 1 && !cli_arg_u32(argv[0], 1UL, 20UL, &secs))) {
        return 1;
    }
    if (!rtc_ready()) {
        return 0;
    }
    console_printf("[rtc] t=%lu rate check over %lu RTC seconds (+~3 s to lock on)...\r\n", T_NOW(), secs);
    int32_t ppm_x10 = 0;
    uint32_t us = 0U;
    status_t st = rtc_check_rate((unsigned)secs, &ppm_x10, &us);
    if (st != ST_OK) {
        console_printf("[rtc] t=%lu rate check %s\r\n", T_NOW(), status_str(st));
        return 0;
    }
    const int32_t a = ppm_x10 < 0 ? -ppm_x10 : ppm_x10;
    console_printf("[rtc] t=%lu rate: %lu RTC s took %lu us of CPU (HSE) time -> RTC %s%ld.%ld ppm (%s)\r\n",
                   T_NOW(), secs, (unsigned long)us, ppm_x10 < 0 ? "-" : "+", (long)(a / 10), (long)(a % 10),
                   ppm_x10 < 0 ? "slow" : "fast");
    return 0;
}

static const cli_cmd_t s_cmds[] = {
    {"rtc", "", "RTC clock source and time (inits on first use)", cmd_rtc},
    {"rtc set", "YYYY-MM-DD hh:mm:ss", "set the RTC calendar", cmd_rtc_set},
    {"rtc check", "[seconds]", "RTC rate vs the HSE crystal, ppm (default 20 s)", cmd_rtc_check},
};

void rtc_cmds_register(void)
{
    cli_register(s_cmds, sizeof s_cmds / sizeof s_cmds[0]);
}
