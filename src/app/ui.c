/*
 * ui.c: UiTask (see ui.h).
 */
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "FreeRTOS.h"
#include "task.h"

#include "app/app_mode.h"
#include "app/cli.h"
#include "app/evlog.h"
#include "app/fmt.h"
#include "app/infer_task.h"
#include "app/sensor_task.h"
#include "app/tasks.h"
#include "app/wdg.h"
#include "app_config.h"
#include "drivers/ap3216c.h"
#include "drivers/aht.h"
#include "drivers/buttons.h"
#include "drivers/buzzer.h"
#include "drivers/console_uart.h"
#include "drivers/es8388.h"
#include "drivers/extflash.h"
#include "drivers/icm20608.h"
#include "drivers/lcd.h"
#include "drivers/led.h"
#include "drivers/led_matrix.h"
#include "drivers/rtc.h"
#include "drivers/rw007.h"
#include "drivers/sdcard.h"
#include "model/model.h"
#include "ui/gfx.h"
#include "ui/screens.h"

#define T_NOW()       ((uint32_t)xTaskGetTickCount())
#define KEY_ECHO_MS   400U
#define ALARM_FLASH_MS 500U
#define LAT_N         128U

static CCM_BSS StackType_t s_stack[UI_TASK_STACK_WORDS];
static CCM_BSS StaticTask_t s_tcb;

static uint32_t s_reset_csr;
static bool s_capture_key;
static bool s_lcd;
static app_mode_t s_return = MODE_RUN; /* mode to go back to from DIAG */
static ui_view_t s_v;
static bool s_redraw_full;
static bool s_alarm;
static uint32_t s_alarm_t0;
static bool s_alarm_phase;
static uint32_t s_alarm_flash_t;
static uint32_t s_banner_t;
static uint32_t s_key_t;
static bool s_swallow[BTN_COUNT];
static unsigned s_page;
static const char *s_diag_msg;
static uint32_t s_last_fault;
static uint32_t s_hot_since;
static bool s_hot_pending;
static volatile bool s_alarm_req; /* `alarm test` (CLI task) -> UiTask */

/* Climate thresholds (RAM; `climate set`). */
static float s_t_high = CLIMATE_T_HIGH_C, s_rh_high = CLIMATE_RH_HIGH;
static uint32_t s_hold_s = CLIMATE_HOLD_S;

static uint32_t s_lat[LAT_N];
static unsigned s_lat_n, s_lat_head;

static const uint8_t k_rgb[MODEL_N_CLASSES][3] = {{0, 0, 40}, {0, 40, 0}, {48, 14, 0}, {40, 0, 40}};

/* ---- helpers ------------------------------------------------------------------- */

static void beep(buzzer_pattern_t p)
{
    if (!s_v.muted) {
        buzzer_play(p);
    }
}

static void matrix_state(void)
{
    if (!led_matrix_is_initialised()) {
        return;
    }
    const int st = s_v.have_result ? s_v.r.dec.state : -1;
    for (unsigned i = 0; i < 19U; i++) {
        if (st >= 0 && !s_v.r.dec.uncertain) {
            led_matrix_set(i, k_rgb[st][0], k_rgb[st][1], k_rgb[st][2]);
        } else {
            led_matrix_set(i, 6, 6, 6);
        }
    }
    if (s_v.mode == MODE_GUARD) {
        led_matrix_set(0, 60, 0, 0); /* guard armed marker */
    }
    (void)led_matrix_show();
}

static void matrix_fill(uint8_t r, uint8_t g, uint8_t b)
{
    if (led_matrix_is_initialised()) {
        led_matrix_fill(r, g, b);
        (void)led_matrix_show();
    }
}

static void clock_text(char *buf, size_t n)
{
    rtc_info_t ri;
    rtc_get_info(&ri);
    rtc_datetime_t dt;
    if (ri.initialised && ri.time_valid && rtc_get(&dt) == ST_OK) {
        (void)snprintf(buf, n, "%02u:%02u:%02u", dt.hour, dt.minute, dt.second);
    } else {
        (void)snprintf(buf, n, "--:--:--");
    }
}

static void draw(void)
{
    if (!s_lcd || s_v.mode == MODE_CAPTURE || s_v.mode == MODE_BOOT) {
        return;
    }
    if (s_alarm) {
        return; /* alarm screen is drawn by its own timer */
    }
    if (s_v.mode == MODE_DIAG) {
        scr_diag(s_page, s_redraw_full, s_diag_msg);
    } else {
        scr_home(&s_v, s_redraw_full);
    }
    s_redraw_full = false;
}

static void lat_add(uint32_t ms)
{
    s_lat[s_lat_head] = ms;
    s_lat_head = (s_lat_head + 1U) % LAT_N;
    if (s_lat_n < LAT_N) {
        s_lat_n++;
    }
}

static int cmp_u32(const void *a, const void *b)
{
    const uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

void ui_get_latency(uint32_t *n, uint32_t *med, uint32_t *p95, uint32_t *mx)
{
    static uint32_t v[LAT_N];
    taskENTER_CRITICAL();
    const unsigned k = s_lat_n;
    memcpy(v, s_lat, sizeof v);
    taskEXIT_CRITICAL();
    *n = k;
    if (k == 0U) {
        *med = *p95 = *mx = 0U;
        return;
    }
    qsort(v, k, sizeof v[0], cmp_u32);
    *med = v[k / 2U];
    *p95 = v[(k * 95U + 99U) / 100U - 1U];
    *mx = v[k - 1U];
}

/* ---- mode changes -------------------------------------------------------------- */

static void alarm_stop(void)
{
    if (s_alarm) {
        s_alarm = false;
        buzzer_stop();
        led_set(LED_RED, false);
        s_redraw_full = true;
    }
}

static void enter_mode(app_mode_t to)
{
    alarm_stop();
    const app_mode_t now = app_mode_switch(to);
    s_v.mode = now;
    if (now == MODE_CAPTURE) {
        return; /* the capture task owns keys, LCD and LEDs now */
    }
    g_hb_led = false;
    led_set(LED_RED, false);
    if (now == MODE_RUN || now == MODE_GUARD) {
        s_return = now;
    }
    buttons_resync();
    s_redraw_full = true;
    s_v.banner = NULL;
    matrix_state();
    draw();
}

/* ---- alerts -------------------------------------------------------------------- */

static void banner(const char *text, uint16_t bg)
{
    s_v.banner = text;
    s_v.banner_bg = bg;
    s_banner_t = T_NOW();
}

static void alarm_start(void)
{
    s_alarm = true;
    s_alarm_t0 = T_NOW();
    s_alarm_flash_t = 0U;
    led_set(LED_RED, true);
    beep(BUZ_ALARM);
}

static void on_result(const infer_result_t *r)
{
    const app_mode_t m = s_v.mode;
    const uint8_t conf = (uint8_t)(r->dec.conf * 100.0f + 0.5f);
    s_v.r = *r;
    s_v.have_result = true;
    s_v.fault = r->fault & ((1UL << SENS_IMU) | (1UL << SENS_ALS) | (1UL << SENS_AHT) | INFER_FAULT_GAP);

    if (s_v.fault != s_last_fault) {
        evlog_post(EV_FAULT, (uint8_t)m, 0xFFU, 0U, s_v.fault, s_last_fault);
        if (s_v.fault != 0U && (s_v.fault & ~s_last_fault) != 0U) {
            beep(BUZ_ERROR);
        }
        s_last_fault = s_v.fault;
    }
    if (r->classified && r->dec.changed) {
        evlog_post(EV_STATE, (uint8_t)m, (uint8_t)r->dec.state, conf, 0U, 0U);
        if (!r->dec.uncertain && r->dec.state == CLS_ROUGH) {
            evlog_post(EV_ROUGH, (uint8_t)m, CLS_ROUGH, conf, 0U, 0U);
            if (m != MODE_DIAG) {
                banner("Rough handling detected", GFX_ORANGE);
                beep(BUZ_CHIRP);
            }
        }
        if (!r->dec.uncertain && r->dec.state == CLS_ACCESS) {
            const bool guarded = (m == MODE_GUARD);
            evlog_post(EV_ACCESS, (uint8_t)m, CLS_ACCESS, conf, guarded ? 1U : 0U, 0U);
            if (guarded && !s_alarm) {
                alarm_start();
                console_printf("[ui] t=%lu GUARD alarm: ACCESS (conf %u %%)\r\n", (unsigned long)T_NOW(), conf);
            }
        }
    }

    /* climate rule (context only, PRD 2.3) */
    if (r->env_ok) {
        const bool hot = r->temp > s_t_high || r->rh > s_rh_high;
        if (!s_v.climate_warn) {
            if (!hot) {
                s_hot_pending = false;
            } else if (!s_hot_pending) {
                s_hot_pending = true;
                s_hot_since = T_NOW();
            } else if ((T_NOW() - s_hot_since) >= s_hold_s * 1000U) {
                s_v.climate_warn = true;
                evlog_post(EV_CLIMATE, (uint8_t)m, 0xFFU, 0U, 1U, 0U);
                banner("Bag too hot/humid!", GFX_ORANGE);
                beep(BUZ_ERROR);
            }
        } else if (r->temp < s_t_high - CLIMATE_HYST_C && r->rh < s_rh_high - CLIMATE_HYST_RH) {
            s_v.climate_warn = false;
            s_hot_pending = false;
            evlog_post(EV_CLIMATE, (uint8_t)m, 0xFFU, 0U, 0U, 0U);
        }
    }

    /* feedback: LEDs, matrix, LCD */
    led_set(LED_BLUE, (r->seq & 1U) != 0U); /* activity: toggles every window */
    if (!s_alarm) {
        if (s_v.banner != NULL && s_v.banner_bg == GFX_ORANGE && r->dec.state == CLS_ROUGH) {
            matrix_fill(48, 14, 0);
        } else {
            matrix_state();
        }
        led_set(LED_RED, s_v.fault != 0U);
    }
    draw();
    const uint32_t lat = T_NOW() - r->t_end_ms;
    taskENTER_CRITICAL();
    lat_add(lat);
    taskEXIT_CRITICAL();

    if (infer_log_enabled() && !g_console_quiet) {
        char p[MODEL_N_CLASSES][FMT_F_BUF], c[FMT_F_BUF];
        for (int k = 0; k < MODEL_N_CLASSES; k++) {
            (void)fmt_f(p[k], sizeof p[k], r->prob[k], 3);
        }
        console_printf("[win] n=%lu t_end=%lu valid=%d raw=%s base=%s p=%s,%s,%s,%s state=%s conf=%s unc=%d "
                       "fault=0x%lx bad=%u/%u t_feat_us=%lu t_inf_us=%lu lat_ms=%lu\r\n",
                       (unsigned long)r->seq, (unsigned long)r->t_end_ms, r->valid ? 1 : 0,
                       r->raw >= 0 ? model_class_name(r->raw) : "-", r->base >= 0 ? model_class_name(r->base) : "-",
                       p[0], p[1], p[2], p[3], r->dec.state >= 0 ? model_class_name(r->dec.state) : "-",
                       fmt_f(c, sizeof c, r->dec.conf, 3), r->dec.uncertain ? 1 : 0, (unsigned long)r->fault,
                       r->imu_bad, r->als_bad, (unsigned long)r->t_feat_us, (unsigned long)r->t_inf_us,
                       (unsigned long)lat);
    }
}

/* ---- keys ---------------------------------------------------------------------- */

static void diag_io_test(void)
{
    s_diag_msg = "running...";
    draw();
    static const uint8_t rgb[3][3] = {{60, 0, 0}, {0, 60, 0}, {0, 0, 60}};
    led_set(LED_RED, true);
    vTaskDelay(pdMS_TO_TICKS(300));
    led_set(LED_RED, false);
    led_set(LED_BLUE, true);
    vTaskDelay(pdMS_TO_TICKS(300));
    led_set(LED_BLUE, false);
    wdg_checkin(WDG_UI);
    for (unsigned i = 0; i < 3U; i++) {
        matrix_fill(rgb[i][0], rgb[i][1], rgb[i][2]);
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    matrix_fill(0, 0, 0);
    wdg_checkin(WDG_UI);
    buzzer_play(BUZ_CHIRP); /* even when muted: this is the buzzer test */
    console_printf("[ui] t=%lu I/O test: red LED, blue LED, matrix R/G/B, buzzer chirp (check by eye/ear)\r\n",
                   (unsigned long)T_NOW());
    s_diag_msg = "done: check by eye and ear";
    matrix_state();
}

static void on_key(const button_event_t *e)
{
    const app_mode_t m = s_v.mode;
    if (e->pressed) {
        /* immediate visible response (AC-5) */
        s_v.key = (int)e->key;
        s_key_t = T_NOW();
        if (s_alarm) {
            const uint32_t ms = T_NOW() - s_alarm_t0;
            alarm_stop();
            s_swallow[e->key] = true;
            evlog_post(EV_ACK, (uint8_t)m, CLS_ACCESS, 0U, ms, (uint32_t)e->key);
            console_printf("[ui] t=%lu alarm acknowledged with %s after %lu ms\r\n", (unsigned long)T_NOW(),
                           button_name(e->key), (unsigned long)ms);
        }
        draw();
        console_printf("[key] t=%lu %s press edge=%lu settled=%lu edge_to_feedback_ms=%lu\r\n",
                       (unsigned long)T_NOW(), button_name(e->key), (unsigned long)e->t_edge_ms,
                       (unsigned long)e->t_ms, (unsigned long)(T_NOW() - e->t_edge_ms));
        return;
    }
    const char *action = "none";
    if (s_swallow[e->key]) {
        s_swallow[e->key] = false;
        action = "after_ack";
    } else if (m == MODE_DIAG) {
        bool io = false;
        switch (e->key) {
        case BTN_LEFT:
            s_page = (s_page + SCR_DIAG_PAGES - 1U) % SCR_DIAG_PAGES;
            s_redraw_full = true;
            action = "page_prev";
            break;
        case BTN_RIGHT:
            s_page = (s_page + 1U) % SCR_DIAG_PAGES;
            s_redraw_full = true;
            action = "page_next";
            break;
        case BTN_UP:
            enter_mode(s_return);
            action = "diag_exit";
            break;
        case BTN_DOWN:
            io = (s_page == 4U);
            action = io ? "io_test" : "none";
            break;
        default: break;
        }
        if (e->key != BTN_UP && !io) {
            s_diag_msg = (s_page == 4U) ? s_diag_msg : NULL;
            draw();
        }
        if (io) {
            /* log first: the test itself takes ~1.5 s (its first step, red LED on, is the response) */
            console_printf("[key] t=%lu %s release held=%lu edge=%lu action=%s edge_to_action_ms=%lu\r\n",
                           (unsigned long)T_NOW(), button_name(e->key), (unsigned long)e->held_ms,
                           (unsigned long)e->t_edge_ms, action, (unsigned long)(T_NOW() - e->t_edge_ms));
            diag_io_test();
            draw();
            return;
        }
    } else {
        switch (e->key) {
        case BTN_LEFT:
            if (e->long_press) {
                enter_mode(m == MODE_GUARD ? MODE_RUN : MODE_GUARD);
                beep(BUZ_CHIRP);
                action = (m == MODE_GUARD) ? "disarm" : "arm";
            }
            break;
        case BTN_UP:
            if (!e->long_press) {
                s_page = 0U;
                s_diag_msg = NULL;
                enter_mode(MODE_DIAG);
                action = "diag_enter";
            }
            break;
        case BTN_DOWN:
            if (e->long_press) {
                s_v.muted = !s_v.muted;
                buzzer_stop();
                console_printf("[ui] t=%lu buzzer %s\r\n", (unsigned long)T_NOW(), s_v.muted ? "muted" : "on");
                if (!s_v.muted) {
                    buzzer_play(BUZ_CHIRP);
                }
                draw();
                action = s_v.muted ? "mute" : "unmute";
            }
            break;
        case BTN_RIGHT:
            s_v.banner = NULL;
            draw();
            action = "clear";
            break;
        default: break;
        }
    }
    /* T7 (PRD 9): release edge -> action done (screen redrawn, chirp started). */
    console_printf("[key] t=%lu %s release held=%lu edge=%lu action=%s edge_to_action_ms=%lu\r\n",
                   (unsigned long)T_NOW(), button_name(e->key), (unsigned long)e->held_ms,
                   (unsigned long)e->t_edge_ms, action, (unsigned long)(T_NOW() - e->t_edge_ms));
}

/* ---- boot self-test (PRD 5.1 BOOT_SELFTEST) ------------------------------------ */

enum { R_OK, R_ABSENT, R_FAIL };

static unsigned s_row;
static unsigned s_n[3];
static uint32_t s_fail_mask;

static void report(const char *name, int res, const char *detail)
{
    static const char *const word[3] = {"OK", "absent", "FAIL"};
    static const uint16_t color[3] = {GFX_RGB(40, 220, 70), GFX_GREY, LCD_RED};
    char b[32];
    (void)snprintf(b, sizeof b, "%s%s%s", word[res], detail[0] ? " " : "", detail);
    console_printf("[selftest] %-10s %s\r\n", name, b);
    if (s_lcd) {
        scr_boot_row(s_row, name, b, color[res]);
    }
    if (res == R_FAIL) {
        s_fail_mask |= 1UL << s_row;
    }
    s_n[res]++;
    s_row++;
    wdg_checkin(WDG_UI);
}

static void boot_selftest(void)
{
    const uint32_t t0 = T_NOW();
    char d[24];
    console_printf("[selftest] t=%lu boot self-test\r\n", (unsigned long)t0);
    s_lcd = lcd_init() == ST_OK;
    if (s_lcd) {
        lcd_backlight(true);
        scr_boot_title();
    }
    report("LCD", s_lcd ? R_OK : R_FAIL, "");
    report("IMU", icm20608_init() == ST_OK ? R_OK : R_FAIL, "");
    report("ALS/PS", ap3216c_init() == ST_OK ? R_OK : R_FAIL, "");
    report("AHT", aht_init() == ST_OK ? R_OK : R_FAIL, "");
    const status_t rs = rtc_init();
    rtc_info_t ri;
    rtc_get_info(&ri);
    (void)snprintf(d, sizeof d, "%s%s", rtc_source_name(ri.source), ri.time_valid ? "" : " time unset");
    report("RTC", rs == ST_OK ? R_OK : R_FAIL, rs == ST_OK ? d : "");
    extflash_info_t fi;
    extflash_get_info(&fi);
    const bool fl = fi.initialised || extflash_init() == ST_OK;
    extflash_get_info(&fi);
    (void)snprintf(d, sizeof d, "%lu KB", (unsigned long)(fi.size_bytes / 1024U));
    report("SPI flash", fl ? R_OK : R_FAIL, fl ? d : "");
    report("LED matrix", led_matrix_init() == ST_OK ? R_OK : R_FAIL, "");
    const status_t ss = sd_init();
    report("SD card", ss == ST_OK ? R_OK : (ss == ST_NOT_PRESENT ? R_ABSENT : R_FAIL), "");
    static uint8_t regs[ES8388_NREGS];
    report("Codec", es8388_dump(regs) == ST_OK ? R_OK : R_FAIL, "");
    rw007_info_t wi;
    const status_t ws = rw007_init(&wi);
    if (ws == ST_OK) {
        (void)snprintf(d, sizeof d, "%.12s, off", wi.version);
        rw007_off(); /* not used by the product; `wifi on` starts it */
    }
    report("Wi-Fi", ws == ST_OK ? R_OK : (ws == ST_NOT_PRESENT ? R_ABSENT : R_FAIL), ws == ST_OK ? d : "");
    (void)snprintf(d, sizeof d, "%s", model_version());
    report("Model", model_available() ? R_OK : R_ABSENT, d);

    const uint32_t ms = T_NOW() - t0;
    console_printf("[selftest] t=%lu boot: %u OK, %u absent, %u FAIL (mask 0x%03lx) in %lu ms\r\n",
                   (unsigned long)T_NOW(), s_n[R_OK], s_n[R_ABSENT], s_n[R_FAIL], (unsigned long)s_fail_mask,
                   (unsigned long)ms);
    if (s_lcd) {
        scr_boot_summary(s_n[R_OK], s_n[R_ABSENT], s_n[R_FAIL], ms);
    }
    buzzer_play(s_n[R_FAIL] ? BUZ_ERROR : BUZ_CHIRP);
    evlog_post(EV_BOOT, MODE_BOOT, 0xFFU, 0U, s_reset_csr, s_fail_mask);
    for (unsigned i = 0; i < UI_SELFTEST_SHOW_MS / 100U; i++) {
        wdg_checkin(WDG_UI);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* ---- task ---------------------------------------------------------------------- */

static void ui_task(void *arg)
{
    (void)arg;
    s_v.key = -1;
    s_v.mode = MODE_BOOT;
    if (s_capture_key) {
        (void)rtc_init(); /* fast when the RTC kept running; gives the events a time */
        evlog_post(EV_BOOT, MODE_BOOT, 0xFFU, 0U, s_reset_csr, 0U);
        s_lcd = lcd_init() == ST_OK;
    } else {
        boot_selftest();
    }
    if (wdg_was_reset()) {
        evlog_post(EV_WDG, MODE_BOOT, 0xFFU, 0U, wdg_prev_missing(), 0U);
    }
    wdg_start();
    enter_mode(s_capture_key ? MODE_CAPTURE : MODE_RUN);

    uint32_t last_draw = T_NOW(), last_clock = 0U;
    for (;;) {
        wdg_checkin(WDG_UI);
        app_mode_t req;
        if (app_mode_take_request(&req)) {
            if (req == MODE_DIAG) {
                s_page = 0U;
            }
            enter_mode(req);
            app_mode_request_done();
        }
        if (s_v.mode == MODE_CAPTURE) {
            vTaskDelay(pdMS_TO_TICKS(UI_PERIOD_MS)); /* capture task owns the I/O */
            continue;
        }

        if (s_alarm_req) {
            s_alarm_req = false;
            if (!s_alarm && (s_v.mode == MODE_RUN || s_v.mode == MODE_GUARD)) {
                alarm_start();
                console_printf("[ui] t=%lu alarm raised by `alarm test` (no detection; acknowledge with a key)\r\n",
                               (unsigned long)T_NOW());
            }
        }
        button_event_t e;
        while (buttons_get_event(&e, 0)) {
            on_key(&e);
        }
        /* Wait for the next window result (feedback at once), at most one period. */
        infer_result_t r;
        if (xQueueReceive(infer_results(), &r, pdMS_TO_TICKS(UI_PERIOD_MS)) == pdPASS) {
            on_result(&r);
        }

        const uint32_t now = T_NOW();
        if (s_alarm && (now - s_alarm_flash_t) >= ALARM_FLASH_MS) {
            s_alarm_flash_t = now;
            s_alarm_phase = !s_alarm_phase;
            led_set(LED_RED, s_alarm_phase);
            matrix_fill(s_alarm_phase ? 80 : 0, 0, 0);
            if (s_lcd) {
                scr_alarm(s_alarm_phase);
            }
        }
        bool redraw = false;
        if (s_v.key >= 0 && (now - s_key_t) >= KEY_ECHO_MS) {
            s_v.key = -1;
            redraw = true;
        }
        if (s_v.banner != NULL && (now - s_banner_t) >= UI_BANNER_MS) {
            s_v.banner = NULL;
            matrix_state();
            redraw = true;
        }
        if ((now - last_clock) >= 1000U) {
            last_clock = now;
            clock_text(s_v.clock, sizeof s_v.clock);
            redraw = true;
        }
        if (s_v.mode == MODE_DIAG && (now - last_draw) >= UI_DIAG_PERIOD_MS) {
            redraw = true;
        }
        if (redraw) {
            last_draw = now;
            draw();
        }
    }
}

/* ---- console ------------------------------------------------------------------- */

static int cmd_climate(int argc, char **argv)
{
    char a[FMT_F_BUF], b[FMT_F_BUF];
    if (argc == 4 && strcmp(argv[0], "set") == 0) {
        unsigned long t, rh, hold;
        if (!cli_arg_u32(argv[1], 0U, 85U, &t) || !cli_arg_u32(argv[2], 1U, 100U, &rh) ||
            !cli_arg_u32(argv[3], 1U, 3600U, &hold)) {
            return 1;
        }
        s_t_high = (float)t;
        s_rh_high = (float)rh;
        s_hold_s = hold;
    } else if (argc != 0) {
        return 1;
    }
    console_printf("[climate] warning when T > %s C or RH > %s %% for %lu s (now %s)\r\n",
                   fmt_f(a, sizeof a, s_t_high, 1), fmt_f(b, sizeof b, s_rh_high, 0), (unsigned long)s_hold_s,
                   s_v.climate_warn ? "WARNING" : "ok");
    return 0;
}

static int cmd_fault(int argc, char **argv)
{
    static const char *const names[SENS_COUNT] = {"imu", "als", "aht"};
    if (argc == 2 && strcmp(argv[0], "inject") == 0) {
        for (unsigned i = 0; i < SENS_COUNT; i++) {
            if (strcmp(argv[1], names[i]) == 0) {
                sensors_inject((sensor_id_t)i, true);
                console_printf("[fault] t=%lu %s reads now fail as a NACK (fault clear %s)\r\n",
                               (unsigned long)T_NOW(), sensor_name((sensor_id_t)i), names[i]);
                return 0;
            }
        }
    } else if (argc == 2 && strcmp(argv[0], "clear") == 0) {
        for (unsigned i = 0; i < SENS_COUNT; i++) {
            if (strcmp(argv[1], names[i]) == 0 || strcmp(argv[1], "all") == 0) {
                sensors_inject((sensor_id_t)i, false);
            }
        }
        console_printf("[fault] t=%lu injection cleared; recovery at the next re-init (<= %u ms)\r\n",
                       (unsigned long)T_NOW(), SENSOR_REINIT_MS);
        return 0;
    } else if (argc == 2 && strcmp(argv[0], "hang") == 0) {
        for (unsigned i = 0; i < WDG_COUNT; i++) {
            if (strcasecmp(argv[1], wdg_name((wdg_id_t)i)) == 0) {
                wdg_request_hang((wdg_id_t)i);
                console_printf("[fault] t=%lu %s stops checking in: watchdog reset expected within %u ms\r\n",
                               (unsigned long)T_NOW(), wdg_name((wdg_id_t)i), WDG_WINDOW_MS + WDG_TIMEOUT_MS);
                return 0;
            }
        }
    } else if (argc == 0) {
        sensor_health_t h[SENS_COUNT];
        sensors_get_health(h);
        for (unsigned i = 0; i < SENS_COUNT; i++) {
            console_printf("[fault] %-6s %s%s consecutive=%lu failures=%lu degraded=%lu reinit=%lu "
                           "recovered=%lu\r\n",
                           sensor_name((sensor_id_t)i), h[i].degraded ? "DEGRADED" : "ok",
                           h[i].injected ? " (injected)" : "", (unsigned long)h[i].consecutive,
                           (unsigned long)h[i].failures, (unsigned long)h[i].degraded_events,
                           (unsigned long)h[i].reinit_tries, (unsigned long)h[i].recoveries);
        }
        wdg_status_t ws;
        wdg_get_status(&ws);
        console_printf("[fault] watchdog %s: windows ok %lu missed %lu\r\n", ws.running ? "running" : "off",
                       (unsigned long)ws.windows_ok, (unsigned long)ws.windows_missed);
        for (unsigned i = 0; i < WDG_COUNT; i++) {
            console_printf("[fault]   %-9s last check-in %lu ms ago\r\n", wdg_name((wdg_id_t)i),
                           (unsigned long)ws.age_ms[i]);
        }
        return 0;
    }
    return 1;
}

static int cmd_ui(int argc, char **argv)
{
    (void)argv;
    if (argc != 0) {
        return 1;
    }
    uint32_t n, med, p95, mx;
    ui_get_latency(&n, &med, &p95, &mx);
    console_printf("[ui] mode %s, alarm %s, muted %s, climate %s, lcd %s; window-close -> feedback over %lu windows: "
                   "median %lu ms p95 %lu ms max %lu ms\r\n",
                   app_mode_name(s_v.mode), s_alarm ? "ON" : "off", s_v.muted ? "yes" : "no",
                   s_v.climate_warn ? "WARNING" : "ok", s_lcd ? "ok" : "none", (unsigned long)n,
                   (unsigned long)med, (unsigned long)p95, (unsigned long)mx);
    return 0;
}

static int cmd_alarm_test(int argc, char **argv)
{
    (void)argv;
    if (argc != 0) {
        return 1;
    }
    if (s_v.mode != MODE_RUN && s_v.mode != MODE_GUARD) {
        console_printf("[ui] alarm test works in RUN or GUARD only (now %s)\r\n", app_mode_name(s_v.mode));
        return 0;
    }
    s_alarm_req = true; /* UiTask raises it within one period (50 ms) */
    return 0;
}

static const cli_cmd_t s_cmds[] = {
    {"ui", "", "UI state and sense-to-feedback latency (AC-4)", cmd_ui},
    {"alarm test", "", "raise the GUARD alarm without a detection (T7 acknowledge timing)", cmd_alarm_test},
    {"climate", "[set <degC> <rh%> <hold_s>]", "climate warning thresholds", cmd_climate},
    {"fault", "[inject|clear <imu|als|aht|all>] [hang <task>]", "sensor health, fault injection (AC-8), watchdog",
     cmd_fault},
};

void ui_task_create(uint32_t reset_csr, bool capture_key)
{
    s_reset_csr = reset_csr;
    s_capture_key = capture_key;
    app_mode_init();
    TaskHandle_t h = xTaskCreateStatic(ui_task, "Ui", UI_TASK_STACK_WORDS, NULL, UI_TASK_PRIO, s_stack, &s_tcb);
    configASSERT(h != NULL);
    cli_register(s_cmds, sizeof s_cmds / sizeof s_cmds[0]);
}
