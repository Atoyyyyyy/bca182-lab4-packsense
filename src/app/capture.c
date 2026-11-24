/*
 * capture.c: CAPTURE mode task and console commands (see capture.h).
 *
 * The capture task owns the trial state, the keys (while in the mode), the LED
 * matrix, the LCD and the buzzer, and is the only writer of the flash log.
 * Console commands post requests to it; `capture dump` and `capture wipe`
 * run in the CLI task and are refused while the mode is on.
 */
#include "capture.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include "app/caplog.h"
#include "app/cli.h"
#include "app/sensor_task.h"
#include "app/tasks.h"
#include "app_config.h"
#include "board_pins.h"
#include "drivers/buttons.h"
#include "drivers/buzzer.h"
#include "drivers/console_uart.h"
#include "drivers/lcd.h"
#include "drivers/led.h"
#include "drivers/led_matrix.h"

#define T_NOW() ((unsigned long)xTaskGetTickCount())

#define NLABELS        4U
#define COUNTDOWN_S    5U
#define UI_PERIOD_MS   250U

static const char *const k_label_name[NLABELS] = {"STILL", "CARRIED", "ROUGH", "ACCESS"};
static const uint16_t k_label_s[NLABELS] = {30U, 30U, 15U, 15U}; /* default trial length */
static const uint8_t k_rgb[NLABELS][3] = {{0, 0, 40}, {0, 40, 0}, {48, 14, 0}, {40, 0, 40}};
static const uint16_t k_lcd[NLABELS] = {0x001FU, 0x07E0U, 0xFC00U, 0xF81FU};

typedef enum { PH_IDLE = 0, PH_COUNTDOWN, PH_REC } phase_t;
typedef enum { RQ_ON = 0, RQ_OFF, RQ_LABEL, RQ_START, RQ_STOP } rq_op_t;

typedef struct {
    rq_op_t op;
    uint32_t arg;
} rq_t;

static CCM_BSS StackType_t s_stack[CAPTURE_TASK_STACK_WORDS];
static CCM_BSS StaticTask_t s_tcb;
static StaticQueue_t s_rq_buf;
static uint8_t s_rq_storage[4 * sizeof(rq_t)];
static QueueHandle_t s_rq;
static StaticSemaphore_t s_ack_buf;
static SemaphoreHandle_t s_ack; /* given after each request is handled */

static volatile bool s_mode;
static volatile phase_t s_phase;
static volatile uint8_t s_label;
static volatile uint16_t s_session; /* 0 until the first trial of this boot */
static volatile uint16_t s_trial;
static volatile uint32_t s_trial_records;
static uint32_t s_phase_t0, s_phase_len_ms, s_trial_len_s;
static uint32_t s_trial_errors;
static uint32_t s_trial_dropped0, s_trial_gaps0;
static bool s_start_in_mode;
static bool s_lcd_ok;

/* ---- feedback ------------------------------------------------------------- */

static void beep(uint32_t on_ms, uint32_t off_ms)
{
    buzzer_set(true);
    vTaskDelay(pdMS_TO_TICKS(on_ms));
    buzzer_set(false);
    if (off_ms) {
        vTaskDelay(pdMS_TO_TICKS(off_ms));
    }
}

static void matrix_show_n(unsigned lit, uint8_t scale_num, uint8_t scale_den)
{
    if (!led_matrix_is_initialised()) {
        return;
    }
    const uint8_t *c = k_rgb[s_label];
    for (unsigned i = 0; i < LEDMX_COUNT; i++) {
        if (i < lit) {
            led_matrix_set(i, (uint8_t)(c[0] * scale_num / scale_den), (uint8_t)(c[1] * scale_num / scale_den),
                           (uint8_t)(c[2] * scale_num / scale_den));
        } else {
            led_matrix_set(i, 0, 0, 0);
        }
    }
    (void)led_matrix_show();
}

/* LCD: label colour background, red band at the top while recording, white
   progress bar, one small square per finished trial of this session. */
static void lcd_draw(bool full)
{
    if (!s_lcd_ok) {
        return;
    }
    const uint16_t W = 240U, H = 240U;
    if (full) {
        lcd_fill(k_lcd[s_label]);
        const unsigned n = s_trial > 40U ? 40U : s_trial;
        for (unsigned k = 0; k < n; k++) {
            lcd_fill_rect((uint16_t)(10U + (k % 20U) * 11U), (uint16_t)(200U + (k / 20U) * 12U), 8U, 8U,
                          LCD_WHITE);
        }
    }
    lcd_fill_rect(0, 0, W, 40, s_phase == PH_REC ? LCD_RED : (s_phase == PH_COUNTDOWN ? LCD_YELLOW : k_lcd[s_label]));
    if (s_phase == PH_REC && s_phase_len_ms) {
        uint32_t el = (uint32_t)xTaskGetTickCount() - s_phase_t0;
        if (el > s_phase_len_ms) {
            el = s_phase_len_ms;
        }
        const uint16_t w = (uint16_t)((W - 20U) * el / s_phase_len_ms);
        lcd_fill_rect(10, 100, w, 30, LCD_WHITE);
    } else if (full) {
        lcd_fill_rect(10, 100, (uint16_t)(W - 20U), 30, k_lcd[s_label]);
    }
    (void)H;
}

static void ui_refresh(bool full)
{
    const uint32_t now = (uint32_t)xTaskGetTickCount();
    switch (s_phase) {
    case PH_IDLE:
        matrix_show_n(LEDMX_COUNT, 1, 3);
        break;
    case PH_COUNTDOWN:
        matrix_show_n(((now / 250U) & 1U) ? LEDMX_COUNT : 0U, 1, 1);
        break;
    case PH_REC: {
        const uint32_t el = now - s_phase_t0;
        const uint32_t left = (el < s_phase_len_ms) ? s_phase_len_ms - el : 0U;
        matrix_show_n((unsigned)((LEDMX_COUNT * left + s_phase_len_ms - 1U) / s_phase_len_ms), 1, 1);
        break;
    }
    }
    lcd_draw(full);
}

/* ---- trial state ------------------------------------------------------------ */

static void enter_mode(void)
{
    if (s_mode) {
        return;
    }
    status_t st = caplog_open();
    caplog_info_t ci;
    caplog_get_info(&ci);
    console_printf("[capture] t=%lu log %s: %lu of %lu records used (%lu min free), last session %u\r\n", T_NOW(),
                   status_str(st), (unsigned long)ci.records, (unsigned long)ci.capacity,
                   (unsigned long)((ci.capacity - ci.records) / 6000U), ci.last_session);
    if (!sensors_start()) {
        console_printf("[capture] t=%lu IMU not present: capture mode not started\r\n", T_NOW());
        return;
    }
    if (!led_matrix_is_initialised()) {
        (void)led_matrix_init();
    }
    if (!lcd_is_initialised()) {
        s_lcd_ok = (lcd_init() == ST_OK);
    } else {
        s_lcd_ok = true;
    }
    g_hb_led = false;
    led_set(LED_RED, false);
    s_phase = PH_IDLE;
    s_mode = true;
    console_printf("[capture] t=%lu CAPTURE mode on, label %s. Keys: LEFT/RIGHT label, UP start, DOWN stop\r\n",
                   T_NOW(), k_label_name[s_label]);
    ui_refresh(true);
    for (unsigned i = 0; i <= s_label; i++) {
        beep(80, 120);
    }
}

static void stop_trial(const char *why)
{
    if (s_phase == PH_REC) {
        status_t st = caplog_flush();
        if (st != ST_OK) {
            s_trial_errors++;
        }
        sensor_stats_t ss;
        sensors_get_stats(&ss);
        caplog_info_t ci;
        caplog_get_info(&ci);
        console_printf("[capture] t=%lu stop (%s): session %u trial %u %s, %lu records (%lu.%02lu s), "
                       "write_errors=%lu dropped=%lu gaps=%lu, log %lu/%lu\r\n",
                       T_NOW(), why, s_session, s_trial, k_label_name[s_label], (unsigned long)s_trial_records,
                       (unsigned long)(s_trial_records / 100U), (unsigned long)(s_trial_records % 100U),
                       (unsigned long)s_trial_errors, (unsigned long)(ss.queue_full - s_trial_dropped0),
                       (unsigned long)(ss.gaps - s_trial_gaps0), (unsigned long)ci.records,
                       (unsigned long)ci.capacity);
        led_set(LED_RED, false);
        s_phase = PH_IDLE;
        ui_refresh(true);
        beep(80, 120);
        beep(80, 0);
    } else if (s_phase == PH_COUNTDOWN) {
        console_printf("[capture] t=%lu countdown cancelled\r\n", T_NOW());
        s_trial--; /* number not used */
        s_phase = PH_IDLE;
        ui_refresh(true);
        beep(300, 0);
    }
}

static void leave_mode(void)
{
    if (!s_mode) {
        return;
    }
    stop_trial("mode off");
    sensors_stop();
    s_mode = false;
    if (led_matrix_is_initialised()) {
        led_matrix_fill(0, 0, 0);
        (void)led_matrix_show();
    }
    g_hb_led = true;
    console_printf("[capture] t=%lu CAPTURE mode off\r\n", T_NOW());
}

static void set_label(uint8_t l)
{
    if (s_phase != PH_IDLE || l >= NLABELS) {
        return;
    }
    s_label = l;
    console_printf("[capture] t=%lu label %u %s (%u s trials)\r\n", T_NOW(), l, k_label_name[l], k_label_s[l]);
    ui_refresh(true);
    for (unsigned i = 0; i <= l; i++) {
        beep(80, 120);
    }
}

static void start_trial(uint32_t seconds)
{
    if (s_phase != PH_IDLE) {
        return;
    }
    caplog_info_t ci;
    caplog_get_info(&ci);
    if (!ci.open || ci.records >= ci.capacity) {
        console_printf("[capture] t=%lu log %s: cannot record\r\n", T_NOW(), ci.open ? "full" : "not open");
        beep(600, 0);
        return;
    }
    if (s_session == 0U) {
        s_session = (uint16_t)(ci.last_session + 1U);
        s_trial = 0U;
    }
    s_trial++;
    s_trial_len_s = seconds ? seconds : k_label_s[s_label];
    s_phase = PH_COUNTDOWN;
    s_phase_t0 = (uint32_t)xTaskGetTickCount();
    s_phase_len_ms = COUNTDOWN_S * 1000U;
    console_printf("[capture] t=%lu session %u trial %u %s: countdown %u s, then %lu s\r\n", T_NOW(), s_session,
                   s_trial, k_label_name[s_label], COUNTDOWN_S, (unsigned long)s_trial_len_s);
    ui_refresh(true);
}

static void begin_recording(void)
{
    sensor_stats_t ss;
    sensors_get_stats(&ss);
    s_trial_dropped0 = ss.queue_full;
    s_trial_gaps0 = ss.gaps;
    s_trial_records = 0U;
    s_trial_errors = 0U;
    beep(300, 0);
    (void)xQueueReset(sensors_queue()); /* trial starts after the beep */
    s_phase = PH_REC;
    s_phase_t0 = (uint32_t)xTaskGetTickCount();
    s_phase_len_ms = s_trial_len_s * 1000U;
    led_set(LED_RED, true);
    console_printf("[capture] t=%lu recording session %u trial %u %s\r\n", T_NOW(), s_session, s_trial,
                   k_label_name[s_label]);
    ui_refresh(true);
}

static void handle_rq(const rq_t *r)
{
    switch (r->op) {
    case RQ_ON: enter_mode(); break;
    case RQ_OFF: leave_mode(); break;
    case RQ_LABEL: set_label((uint8_t)r->arg); break;
    case RQ_START: start_trial(r->arg); break;
    case RQ_STOP: stop_trial("stop"); break;
    }
    (void)xSemaphoreGive(s_ack);
}

static void handle_key(const button_event_t *e)
{
    if (!e->pressed) {
        return;
    }
    switch (e->key) {
    case BTN_LEFT: set_label((uint8_t)((s_label + NLABELS - 1U) % NLABELS)); break;
    case BTN_RIGHT: set_label((uint8_t)((s_label + 1U) % NLABELS)); break;
    case BTN_UP: start_trial(0U); break;
    case BTN_DOWN: stop_trial("key"); break;
    default: break;
    }
}

static void capture_task(void *arg)
{
    (void)arg;
    if (s_start_in_mode) {
        enter_mode();
    }
    uint32_t last_ui = 0U;
    uint32_t last_tick_s = 0U;
    for (;;) {
        rq_t r;
        if (!s_mode) {
            if (xQueueReceive(s_rq, &r, portMAX_DELAY) == pdPASS) {
                handle_rq(&r);
            }
            continue;
        }
        while (xQueueReceive(s_rq, &r, 0) == pdPASS) {
            handle_rq(&r);
        }
        if (!s_mode) {
            continue;
        }
        button_event_t e;
        while (buttons_get_event(&e, 0)) {
            handle_key(&e);
        }

        /* Samples: kept only while recording. */
        sample_t s;
        while (xQueueReceive(sensors_queue(), &s, pdMS_TO_TICKS(10)) == pdPASS) {
            if (s_phase == PH_REC) {
                caplog_rec_t rec;
                caplog_pack(&s, s_label, s_trial, s_session, &rec);
                status_t st = caplog_append(&rec);
                if (st == ST_OK) {
                    s_trial_records++;
                } else {
                    s_trial_errors++;
                    if (st == ST_RANGE_ERR) {
                        stop_trial("log full");
                        break;
                    }
                }
            }
            if (uxQueueMessagesWaiting(sensors_queue()) == 0U) {
                break;
            }
        }

        uint32_t now = (uint32_t)xTaskGetTickCount();
        if (s_phase == PH_COUNTDOWN) {
            const uint32_t el = now - s_phase_t0;
            const uint32_t sec = el / 1000U;
            if (el >= s_phase_len_ms) {
                begin_recording();
            } else if (sec != last_tick_s) {
                last_tick_s = sec;
                beep(30, 0);
            }
        } else {
            last_tick_s = 0U;
        }
        now = (uint32_t)xTaskGetTickCount(); /* begin_recording() may have reset s_phase_t0 */
        if (s_phase == PH_REC && (now - s_phase_t0) >= s_phase_len_ms) {
            stop_trial("time");
        }
        if (now - last_ui >= UI_PERIOD_MS) {
            last_ui = now;
            ui_refresh(false);
        }
    }
}

/* ---- console ------------------------------------------------------------------ */

static void post(rq_op_t op, uint32_t arg)
{
    rq_t r = {op, arg};
    (void)xSemaphoreTake(s_ack, 0);
    if (xQueueSend(s_rq, &r, pdMS_TO_TICKS(100)) == pdPASS) {
        (void)xSemaphoreTake(s_ack, pdMS_TO_TICKS(5000)); /* its output comes before the prompt */
    }
}

static int cmd_capture(int argc, char **argv)
{
    (void)argv;
    if (argc != 0) {
        return 1;
    }
    if (!s_mode) {
        (void)caplog_open();
    }
    caplog_info_t ci;
    caplog_get_info(&ci);
    sensor_stats_t ss;
    sensors_get_stats(&ss);
    static const char *const ph[] = {"idle", "countdown", "recording"};
    console_printf("[capture] t=%lu mode=%s label=%s phase=%s session=%u trial=%u\r\n", T_NOW(),
                   s_mode ? "CAPTURE" : "off", k_label_name[s_label], ph[s_phase], s_session, s_trial);
    console_printf("[capture] log: %lu of %lu records (%lu.%lu min used, %lu min free), last session %u, "
                   "write_errors=%lu erases=%lu\r\n",
                   (unsigned long)ci.records, (unsigned long)ci.capacity, (unsigned long)(ci.records / 6000U),
                   (unsigned long)((ci.records % 6000U) / 600U), (unsigned long)((ci.capacity - ci.records) / 6000U),
                   ci.last_session, (unsigned long)ci.write_errors, (unsigned long)ci.erases);
    console_printf("[capture] sensors: running=%d samples=%lu imu_err=%lu als_err=%lu env_err=%lu gaps=%lu "
                   "queue_full=%lu max_period=%lu ms env_reads=%lu\r\n",
                   ss.running, (unsigned long)ss.samples, (unsigned long)ss.imu_errors,
                   (unsigned long)ss.als_errors, (unsigned long)ss.env_errors, (unsigned long)ss.gaps,
                   (unsigned long)ss.queue_full, (unsigned long)ss.max_period_ms, (unsigned long)ss.env_reads);
    return 0;
}

static int cmd_capture_on(int argc, char **argv)
{
    (void)argv;
    if (argc != 0) {
        return 1;
    }
    post(RQ_ON, 0);
    return 0;
}

static int cmd_capture_off(int argc, char **argv)
{
    (void)argv;
    if (argc != 0) {
        return 1;
    }
    post(RQ_OFF, 0);
    return 0;
}

static int cmd_capture_label(int argc, char **argv)
{
    if (argc != 1) {
        return 1;
    }
    for (unsigned i = 0; i < NLABELS; i++) {
        if (strcasecmp(argv[0], k_label_name[i]) == 0) {
            post(RQ_LABEL, i);
            return 0;
        }
    }
    unsigned long v;
    if (!cli_arg_u32(argv[0], 0UL, NLABELS - 1UL, &v)) {
        return 1;
    }
    post(RQ_LABEL, v);
    return 0;
}

static int cmd_capture_start(int argc, char **argv)
{
    unsigned long s = 0UL;
    if (argc > 1 || (argc == 1 && !cli_arg_u32(argv[0], 2UL, 600UL, &s))) {
        return 1;
    }
    if (!s_mode) {
        console_printf("[capture] not in CAPTURE mode (capture on)\r\n");
        return 0;
    }
    post(RQ_START, s);
    return 0;
}

static int cmd_capture_stop(int argc, char **argv)
{
    (void)argv;
    if (argc != 0) {
        return 1;
    }
    post(RQ_STOP, 0);
    return 0;
}

/* -- dump: base64 lines of up to 8 records with a CRC-16/CCITT ------------- */

static uint16_t crc16(const uint8_t *p, size_t n)
{
    uint16_t c = 0xFFFFU;
    while (n--) {
        c ^= (uint16_t)(*p++ << 8);
        for (int k = 0; k < 8; k++) {
            c = (c & 0x8000U) ? (uint16_t)((c << 1) ^ 0x1021U) : (uint16_t)(c << 1);
        }
    }
    return c;
}

static size_t b64(const uint8_t *in, size_t n, char *out)
{
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        const uint32_t v = ((uint32_t)in[i] << 16) | ((i + 1 < n) ? (uint32_t)in[i + 1] << 8 : 0U) |
                           ((i + 2 < n) ? (uint32_t)in[i + 2] : 0U);
        out[o++] = t[(v >> 18) & 63U];
        out[o++] = t[(v >> 12) & 63U];
        out[o++] = (i + 1 < n) ? t[(v >> 6) & 63U] : '=';
        out[o++] = (i + 2 < n) ? t[v & 63U] : '=';
    }
    return o;
}

static int cmd_capture_dump(int argc, char **argv)
{
    unsigned long from = 0UL, baud = 0UL;
    if (argc > 2 || (argc >= 1 && !cli_arg_u32(argv[0], 0UL, CAPLOG_CAPACITY, &from)) ||
        (argc == 2 && !cli_arg_u32(argv[1], 9600UL, 2000000UL, &baud))) {
        return 1;
    }
    if (s_mode) {
        console_printf("[capture] leave CAPTURE mode first (capture off)\r\n");
        return 0;
    }
    status_t st = caplog_open();
    caplog_info_t ci;
    caplog_get_info(&ci);
    if (st != ST_OK) {
        console_printf("[capture] log %s\r\n", status_str(st));
        return 0;
    }
    const uint32_t n = ci.records;
    console_printf("#CAPDUMP v1 records=%lu from=%lu rec_size=%u acc_scale=800 gyr_scale=60 als_lsb=0.35 "
                   "baud=%lu\r\n",
                   (unsigned long)n, from, CAPLOG_REC_SIZE, baud ? baud : (unsigned long)CONSOLE_BAUD);
    if (baud) {
        vTaskDelay(pdMS_TO_TICKS(100));
        console_set_baud(baud);
        vTaskDelay(pdMS_TO_TICKS(400)); /* host switches its port */
    }
    static uint8_t buf[8 * CAPLOG_REC_SIZE];
    static char line[16 + 344 + 8];
    uint32_t lines = 0U, read_errors = 0U;
    for (uint32_t i = from; i < n; i += 8U) {
        const uint32_t k = (n - i < 8U) ? n - i : 8U;
        if (caplog_read(i, (caplog_rec_t *)buf, k) != ST_OK) {
            read_errors++;
            continue;
        }
        int h = snprintf(line, sizeof line, "P,%lu,", (unsigned long)i);
        size_t len = (size_t)h + b64(buf, k * CAPLOG_REC_SIZE, &line[h]);
        len += (size_t)snprintf(&line[len], sizeof line - len, ",%04X\r\n", crc16(buf, k * CAPLOG_REC_SIZE));
        console_write(line, len);
        lines++;
    }
    console_printf("#END records=%lu lines=%lu read_errors=%lu\r\n", (unsigned long)(n > from ? n - from : 0U),
                   (unsigned long)lines, (unsigned long)read_errors);
    if (baud) {
        console_set_baud(CONSOLE_BAUD);
        vTaskDelay(pdMS_TO_TICKS(400));
        console_printf("#BAUD %lu\r\n", (unsigned long)CONSOLE_BAUD);
    }
    return 0;
}

static int cmd_capture_wipe(int argc, char **argv)
{
    if (argc != 1 || strcmp(argv[0], "yes") != 0) {
        console_printf("[capture] this erases every recorded trial: type 'capture wipe yes'\r\n");
        return 0;
    }
    if (s_mode) {
        console_printf("[capture] leave CAPTURE mode first (capture off)\r\n");
        return 0;
    }
    (void)caplog_open();
    const uint32_t t0 = (uint32_t)xTaskGetTickCount();
    status_t st = caplog_wipe();
    console_printf("[capture] t=%lu wipe %s in %lu ms\r\n", T_NOW(), status_str(st),
                   (unsigned long)((uint32_t)xTaskGetTickCount() - t0));
    s_session = 0U;
    s_trial = 0U;
    return 0;
}

static const cli_cmd_t s_cmds[] = {
    {"capture", "", "capture mode / log / sensor status", cmd_capture},
    {"capture on", "", "enter CAPTURE mode (sensors on, keys label/start/stop)", cmd_capture_on},
    {"capture off", "", "leave CAPTURE mode", cmd_capture_off},
    {"capture label", "<0-3|name>", "STILL, CARRIED, ROUGH, ACCESS", cmd_capture_label},
    {"capture start", "[seconds]", "start a trial (5 s countdown; default length per label)", cmd_capture_start},
    {"capture stop", "", "stop the trial / cancel the countdown", cmd_capture_stop},
    {"capture dump", "[from] [baud]", "print the log as base64 lines (tools/capture.py)", cmd_capture_dump},
    {"capture wipe", "yes", "erase the whole capture log", cmd_capture_wipe},
};

void capture_init(bool start_in_mode)
{
    s_start_in_mode = start_in_mode;
    s_rq = xQueueCreateStatic(4, sizeof(rq_t), s_rq_storage, &s_rq_buf);
    s_ack = xSemaphoreCreateBinaryStatic(&s_ack_buf);
    TaskHandle_t h = xTaskCreateStatic(capture_task, "Capture", CAPTURE_TASK_STACK_WORDS, NULL, CAPTURE_TASK_PRIO,
                                       s_stack, &s_tcb);
    configASSERT(s_rq != NULL && h != NULL);
    cli_register(s_cmds, sizeof s_cmds / sizeof s_cmds[0]);
}
