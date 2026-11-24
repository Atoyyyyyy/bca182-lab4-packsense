/*
 * screens.c: see screens.h. 240 x 240 panel; small font 8 x 16 (30 columns).
 */
#include "screens.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "app/evlog.h"
#include "app/fmt.h"
#include "app/sensor_task.h"
#include "app/wdg.h"
#include "drivers/buttons.h"
#include "drivers/extflash.h"
#include "drivers/rtc.h"
#include "drivers/rw007.h"
#include "drivers/sdcard.h"
#include "gfx.h"
#include "model/model.h"

#define COLS     29U
#define X0       4U
#define ROW_H    16U
#define FOOT_Y   224U
#define HEAD_H   20U

static void line(uint16_t y, const char *s, uint16_t fg, uint16_t bg)
{
    gfx_text_pad(X0, y, s, COLS, FONT_SMALL, 1, fg, bg);
}

uint16_t scr_state_color(int state)
{
    switch (state) {
    case CLS_STILL: return GFX_RGB(70, 150, 255);
    case CLS_CARRIED: return GFX_RGB(40, 220, 70);
    case CLS_ROUGH: return GFX_ORANGE;
    case CLS_ACCESS: return GFX_MAGENTA;
    default: return GFX_GREY;
    }
}

void scr_fault_text(uint32_t fault, char *buf, unsigned n)
{
    (void)snprintf(buf, n, "Sensor fault -%s%s%s%s", (fault & (1UL << SENS_IMU)) ? " IMU" : "",
                   (fault & (1UL << SENS_ALS)) ? " ALS/PS" : "", (fault & (1UL << SENS_AHT)) ? " AHT" : "",
                   (fault & INFER_FAULT_GAP) ? " gap" : "");
}

static void header(const char *text, uint16_t bg)
{
    lcd_fill_rect(0, 0, LCD_WIDTH, 2, bg);
    gfx_text_pad(0, 2, text, 30, FONT_SMALL, 1, LCD_WHITE, bg);
    lcd_fill_rect(0, 18, LCD_WIDTH, 2, bg);
}

/* ---- boot ---------------------------------------------------------------------- */

void scr_boot_title(void)
{
    lcd_fill(LCD_BLACK);
    header(" PackSense self-test", GFX_DKBLUE);
}

void scr_boot_row(unsigned row, const char *name, const char *result, uint16_t color)
{
    char b[40];
    (void)snprintf(b, sizeof b, "%-11s %s", name, result);
    line((uint16_t)(HEAD_H + 4U + row * ROW_H), b, color, LCD_BLACK);
}

void scr_boot_summary(unsigned ok, unsigned absent, unsigned fail, uint32_t ms)
{
    char b[40];
    (void)snprintf(b, sizeof b, "%u OK %u absent %u FAIL %lu ms", ok, absent, fail, (unsigned long)ms);
    line(FOOT_Y, b, fail ? LCD_WHITE : LCD_BLACK, fail ? LCD_RED : GFX_RGB(40, 220, 70));
}

/* ---- home ---------------------------------------------------------------------- */

static void why_line(unsigned i, const char *label, const char *value)
{
    char b[40];
    (void)snprintf(b, sizeof b, "%-11s%s", label, value);
    line((uint16_t)(100U + i * ROW_H), b, LCD_WHITE, LCD_BLACK);
}

void scr_home(const ui_view_t *v, bool full)
{
    char b[40], f[FMT_F_BUF], g[FMT_F_BUF];
    const infer_result_t *r = &v->r;
    if (full) {
        lcd_fill(LCD_BLACK);
    }
    const uint16_t hb = v->mode == MODE_GUARD ? GFX_RGB(160, 0, 0) : (v->mode == MODE_DIAG ? GFX_GREY : GFX_DKBLUE);
    (void)snprintf(b, sizeof b, " %-6s%s %s", app_mode_name(v->mode), v->muted ? " MUTE" : "     ", "");
    {
        char h[40];
        (void)snprintf(h, sizeof h, "%-20s%s", b, v->clock);
        header(h, hb);
    }

    /* state + confidence */
    const char *name;
    int st = -1;
    bool unc = false;
    if (!v->have_result) {
        name = "STARTING";
    } else if (!model_available()) {
        name = "NO MODEL";
    } else if (r->dec.state < 0) {
        name = "...";
    } else {
        st = r->dec.state;
        unc = r->dec.uncertain;
        name = model_class_name(st);
    }
    char nm[12];
    (void)snprintf(nm, sizeof nm, "%s%s", name, unc ? "?" : "");
    gfx_text_center(26, nm, FONT_LARGE, 2, scr_state_color(st), LCD_BLACK);

    if (!v->have_result) {
        (void)snprintf(b, sizeof b, "collecting 2 s of samples");
    } else if (!r->valid) {
        (void)snprintf(b, sizeof b, "window rejected: last state");
    } else if (!model_available()) {
        (void)snprintf(b, sizeof b, "no model: capture + train");
    } else if (unc) {
        (void)snprintf(b, sizeof b, "uncertain (%u %%)", (unsigned)(r->dec.conf * 100.0f + 0.5f));
    } else {
        (void)snprintf(b, sizeof b, "confidence %u %%", (unsigned)(r->dec.conf * 100.0f + 0.5f));
    }
    gfx_text_center(78, b, FONT_SMALL, 1, GFX_GREY, LCD_BLACK);

    /* why */
    const bool w = v->have_result && r->valid;
    why_line(0, "motion sd", w ? (snprintf(b, sizeof b, "%s g", fmt_f(f, sizeof f, r->why[WHY_MOTION_G], 3)), b) : "-");
    why_line(1, "jerk peak", w ? (snprintf(b, sizeof b, "%s m/s2", fmt_f(f, sizeof f, r->why[WHY_JERK], 2)), b) : "-");
    why_line(2, "light", w ? (snprintf(b, sizeof b, "%s lux", fmt_f(f, sizeof f, r->why[WHY_LUX], 1)), b) : "-");
    why_line(3, "proximity", w ? fmt_f(f, sizeof f, r->why[WHY_PROX], 0) : "-");
    why_line(4, "tilt", w ? (snprintf(b, sizeof b, "%s deg", fmt_f(f, sizeof f, r->why[WHY_TILT], 1)), b) : "-");

    /* climate */
    if (!v->have_result || !r->env_ok) {
        line(184, "climate: AHT not available", GFX_GREY, LCD_BLACK);
    } else if (v->climate_warn) {
        line(184, "HOT/HUMID! check laptop", LCD_BLACK, GFX_ORANGE);
    } else {
        (void)snprintf(b, sizeof b, "%s C  %s %%RH  ok", fmt_f(f, sizeof f, r->temp, 1), fmt_f(g, sizeof g, r->rh, 0));
        line(184, b, GFX_RGB(150, 220, 150), LCD_BLACK);
    }

    /* banner / fault */
    if (v->banner != NULL) {
        line(204, v->banner, LCD_BLACK, v->banner_bg);
    } else if (v->fault != 0U) {
        scr_fault_text(v->fault, b, sizeof b);
        line(204, b, LCD_WHITE, LCD_RED);
    } else {
        line(204, "", LCD_BLACK, LCD_BLACK);
    }

    /* footer: key echo (AC-5) or hints */
    if (v->key >= 0) {
        (void)snprintf(b, sizeof b, "key %s", button_name((button_id_t)v->key));
        line(FOOT_Y, b, LCD_BLACK, LCD_WHITE);
    } else {
        line(FOOT_Y, "Lhold:guard U:diag Dhold:mute", GFX_GREY, LCD_BLACK);
    }
}

void scr_alarm(bool phase)
{
    const uint16_t bg = phase ? LCD_RED : LCD_BLACK, fg = phase ? LCD_WHITE : LCD_RED;
    lcd_fill(bg);
    gfx_text_center(60, "ACCESS!", FONT_LARGE, 2, fg, bg);
    gfx_text_center(130, "bag opened while guarded", FONT_SMALL, 1, fg, bg);
    gfx_text_center(170, "press any key", FONT_LARGE, 1, fg, bg);
}

/* ---- DIAG ---------------------------------------------------------------------- */

static const char *health(const sensor_health_t *h)
{
    return h->injected ? "INJECTED" : (h->degraded ? "DEGRADED" : "OK");
}

static unsigned page_sensors(char b[][40])
{
    char f[3][FMT_F_BUF];
    sample_t s;
    bool have;
    infer_get_last_sample(&s, &have);
    sensor_stats_t ss;
    sensors_get_stats(&ss);
    sensor_health_t h[SENS_COUNT];
    sensors_get_health(h);
    unsigned n = 0;
    if (have) {
        (void)snprintf(b[n++], 40, "a %s %s %s", fmt_f(f[0], sizeof f[0], s.ax, 2), fmt_f(f[1], sizeof f[1], s.ay, 2),
                       fmt_f(f[2], sizeof f[2], s.az, 2));
        (void)snprintf(b[n++], 40, "g %s %s %s", fmt_f(f[0], sizeof f[0], s.gx, 1), fmt_f(f[1], sizeof f[1], s.gy, 1),
                       fmt_f(f[2], sizeof f[2], s.gz, 1));
        (void)snprintf(b[n++], 40, "light %s lux ps %u%s", fmt_f(f[0], sizeof f[0], s.als, 1), s.ps,
                       s.ps_valid ? "" : " inv");
        (void)snprintf(b[n++], 40, "%s C %s %%RH%s", fmt_f(f[0], sizeof f[0], s.temp, 2),
                       fmt_f(f[1], sizeof f[1], s.rh, 1), (s.flags & SF_ENV_ERR) ? " (err)" : "");
    } else {
        (void)snprintf(b[n++], 40, "no samples (sensors stopped)");
    }
    (void)snprintf(b[n++], 40, "units m/s2, deg/s");
    for (unsigned i = 0; i < SENS_COUNT; i++) {
        (void)snprintf(b[n++], 40, "%-6s %-8s fail %lu", sensor_name((sensor_id_t)i), health(&h[i]),
                       (unsigned long)h[i].failures);
    }
    (void)snprintf(b[n++], 40, "samples %lu", (unsigned long)ss.samples);
    (void)snprintf(b[n++], 40, "gaps %lu dropped %lu", (unsigned long)ss.gaps, (unsigned long)ss.queue_full);
    (void)snprintf(b[n++], 40, "max period %lu ms", (unsigned long)ss.max_period_ms);
    return n;
}

static unsigned page_tasks(char b[][40])
{
    static TaskStatus_t st[16];
    configRUN_TIME_COUNTER_TYPE total = 0U;
    const UBaseType_t nt = uxTaskGetSystemState(st, 16, &total);
    unsigned n = 0;
    (void)snprintf(b[n++], 40, "heap free %u min %u", (unsigned)xPortGetFreeHeapSize(),
                   (unsigned)xPortGetMinimumEverFreeHeapSize());
    (void)snprintf(b[n++], 40, "task       hwm(w) cpu%%");
    for (UBaseType_t i = 0; i < nt && n < 12U; i++) {
        const uint32_t pm = total ? (uint32_t)((st[i].ulRunTimeCounter * 1000U) / total) : 0U;
        (void)snprintf(b[n++], 40, "%-10.10s %6lu %3lu.%lu", st[i].pcTaskName,
                       (unsigned long)st[i].usStackHighWaterMark, (unsigned long)(pm / 10U),
                       (unsigned long)(pm % 10U));
    }
    return n;
}

extern void ui_get_latency(uint32_t *n, uint32_t *med, uint32_t *p95, uint32_t *mx);

static unsigned page_infer(char b[][40])
{
    infer_stats_t s;
    infer_get_stats(&s);
    infer_result_t r;
    infer_get_last(&r);
    uint32_t ln, lmed, lp95, lmax;
    ui_get_latency(&ln, &lmed, &lp95, &lmax);
    char f[4][FMT_F_BUF];
    unsigned n = 0;
    (void)snprintf(b[n++], 40, "model %s %s", model_available() ? "v" : "none", model_version());
    (void)snprintf(b[n++], 40, "windows %lu valid %lu", (unsigned long)s.windows, (unsigned long)s.valid);
    (void)snprintf(b[n++], 40, "classified %lu rej %lu", (unsigned long)s.classified, (unsigned long)s.rejected);
    (void)snprintf(b[n++], 40, "feat us med %lu p95 %lu", (unsigned long)s.timing.feat_med,
                   (unsigned long)s.timing.feat_p95);
    (void)snprintf(b[n++], 40, "feat us max %lu", (unsigned long)s.timing.feat_max);
    (void)snprintf(b[n++], 40, "mlp us med %lu p95 %lu", (unsigned long)s.timing.inf_med,
                   (unsigned long)s.timing.inf_p95);
    (void)snprintf(b[n++], 40, "lat ms med %lu p95 %lu", (unsigned long)lmed, (unsigned long)lp95);
    (void)snprintf(b[n++], 40, "lat ms max %lu (n %lu)", (unsigned long)lmax, (unsigned long)ln);
    if (r.classified) {
        (void)snprintf(b[n++], 40, "raw %s base %s", model_class_name(r.raw), model_class_name(r.base));
        (void)snprintf(b[n++], 40, "p %s %s %s %s", fmt_f(f[0], sizeof f[0], r.prob[0], 2),
                       fmt_f(f[1], sizeof f[1], r.prob[1], 2), fmt_f(f[2], sizeof f[2], r.prob[2], 2),
                       fmt_f(f[3], sizeof f[3], r.prob[3], 2));
    } else {
        (void)snprintf(b[n++], 40, "last window not classified");
    }
    return n;
}

static unsigned page_system(char b[][40])
{
    unsigned n = 0;
    rtc_info_t ri;
    rtc_get_info(&ri);
    rtc_datetime_t dt;
    if (ri.initialised && rtc_get(&dt) == ST_OK) {
        (void)snprintf(b[n++], 40, "%04u-%02u-%02u %02u:%02u:%02u%s", dt.year, dt.month, dt.day, dt.hour, dt.minute,
                       dt.second, ri.time_valid ? "" : " unset");
    } else {
        (void)snprintf(b[n++], 40, "RTC not running");
    }
    (void)snprintf(b[n++], 40, "RTC clock %s", rtc_source_name(ri.source));
    extflash_info_t fi;
    extflash_get_info(&fi);
    (void)snprintf(b[n++], 40, "flash %02X%02X%02X %lu KB", fi.jedec[0], fi.jedec[1], fi.jedec[2],
                   (unsigned long)(fi.size_bytes / 1024U));
    evlog_info_t li;
    evlog_get_info(&li);
    (void)snprintf(b[n++], 40, "event log %lu/%lu boot %u", (unsigned long)li.count, (unsigned long)EVLOG_CAPACITY,
                   li.boot);
    (void)snprintf(b[n++], 40, "log errors %lu dropped %lu", (unsigned long)li.write_errors,
                   (unsigned long)li.dropped);
    sd_info_t si;
    sd_get_info(&si);
    (void)snprintf(b[n++], 40, "SD card %s", si.present ? "present" : "absent");
    (void)snprintf(b[n++], 40, "Wi-Fi %s", rw007_is_ready() ? "on (RW007 ready)" : "off");
    wdg_status_t ws;
    wdg_get_status(&ws);
    (void)snprintf(b[n++], 40, "wdg %s ok %lu miss %lu", ws.running ? "on" : "off", (unsigned long)ws.windows_ok,
                   (unsigned long)ws.windows_missed);
    const uint32_t up = (uint32_t)xTaskGetTickCount() / 1000U;
    (void)snprintf(b[n++], 40, "uptime %lu:%02lu:%02lu", (unsigned long)(up / 3600U),
                   (unsigned long)(up / 60U % 60U), (unsigned long)(up % 60U));
    return n;
}

void scr_diag(unsigned page, bool full, const char *action_msg)
{
    static const char *const titles[SCR_DIAG_PAGES] = {"Sensors", "Tasks", "Inference", "System", "I/O test"};
    static char b[13][40];
    if (full) {
        lcd_fill(LCD_BLACK);
    }
    char h[40];
    (void)snprintf(h, sizeof h, " DIAG %u/%u %s", page + 1U, SCR_DIAG_PAGES, titles[page % SCR_DIAG_PAGES]);
    header(h, GFX_GREY);
    unsigned n = 0;
    switch (page) {
    case 0: n = page_sensors(b); break;
    case 1: n = page_tasks(b); break;
    case 2: n = page_infer(b); break;
    case 3: n = page_system(b); break;
    default:
        (void)snprintf(b[n++], 40, "DOWN: red + blue LED,");
        (void)snprintf(b[n++], 40, "LED matrix R/G/B, buzzer");
        (void)snprintf(b[n++], 40, "%s", action_msg ? action_msg : "");
        break;
    }
    for (unsigned i = 0; i < 12U; i++) {
        line((uint16_t)(HEAD_H + 2U + i * ROW_H), i < n ? b[i] : "", LCD_WHITE, LCD_BLACK);
    }
    line(FOOT_Y, page == 4U ? "L/R page U:exit D:run test" : "L/R page U:exit", GFX_GREY, LCD_BLACK);
}
