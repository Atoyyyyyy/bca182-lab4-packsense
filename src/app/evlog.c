/*
 * evlog.c: flash event ring + LogTask (see evlog.h).
 */
#include "evlog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include "app/app_mode.h"
#include "app/cli.h"
#include "app/infer_task.h"
#include "app/wdg.h"
#include "app_config.h"
#include "drivers/console_uart.h"
#include "drivers/extflash.h"
#include "drivers/rtc.h"
#include "model/model.h"

#define ERASED 0xFFFFFFFFUL

_Static_assert(sizeof(evlog_rec_t) == EVLOG_REC_SIZE, "event record must be 32 bytes");

static CCM_BSS StackType_t s_stack[LOG_TASK_STACK_WORDS];
static CCM_BSS StaticTask_t s_tcb;
static StaticQueue_t s_q_buf;
static uint8_t s_q_storage[LOG_QUEUE_LEN * sizeof(evlog_rec_t)];
static QueueHandle_t s_q;
static StaticSemaphore_t s_mx_buf;
static SemaphoreHandle_t s_mx; /* s_info + flash access of this module */
static evlog_info_t s_info;
static volatile uint32_t s_dropped;

static const char *const k_names[EV_TYPE_COUNT] = {"?",     "BOOT",  "MODE",    "STATE", "ROUGH", "ACCESS",
                                                    "ACK",   "FAULT", "CLIMATE", "WDG"};

const char *evlog_type_name(uint8_t type)
{
    return type < EV_TYPE_COUNT ? k_names[type] : "?";
}

static uint16_t rec_check(const evlog_rec_t *r)
{
    const uint8_t *p = (const uint8_t *)r;
    uint16_t s = 0U;
    for (unsigned i = 0; i < EVLOG_REC_SIZE - 2U; i++) {
        s = (uint16_t)(s + p[i]);
    }
    return (uint16_t)(0x5A5AU ^ s);
}

static uint32_t slot_addr(uint32_t i)
{
    return EVLOG_BASE + i * EVLOG_REC_SIZE;
}

static bool read_rec(uint32_t i, evlog_rec_t *r)
{
    return extflash_read(slot_addr(i), (uint8_t *)r, EVLOG_REC_SIZE) == ST_OK;
}

/* ---- RTC time as seconds since 2000 ------------------------------------------ */

static uint32_t days_from_2000(unsigned y, unsigned m, unsigned d)
{
    static const uint16_t cum[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    uint32_t days = (y - 2000U) * 365U + (y - 2000U + 3U) / 4U; /* leap days before year y (2000 is leap) */
    days += cum[(m - 1U) % 12U] + (d - 1U);
    if (m > 2U && (y % 4U) == 0U) {
        days++;
    }
    return days;
}

static uint32_t rtc_seconds(void)
{
    rtc_info_t ri;
    rtc_get_info(&ri);
    rtc_datetime_t dt;
    if (!ri.initialised || !ri.time_valid || rtc_get(&dt) != ST_OK) {
        return 0U;
    }
    return days_from_2000(dt.year, dt.month, dt.day) * 86400UL + dt.hour * 3600UL + dt.minute * 60UL + dt.second;
}

static void fmt_rtc(uint32_t s, char *buf, size_t n)
{
    if (s == 0U) {
        (void)snprintf(buf, n, "rtc-unset");
        return;
    }
    uint32_t days = s / 86400UL, rem = s % 86400UL;
    unsigned y = 2000U;
    for (;;) {
        const uint32_t len = (y % 4U == 0U) ? 366U : 365U;
        if (days < len) {
            break;
        }
        days -= len;
        y++;
    }
    static const uint8_t mlen[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    unsigned m = 0U;
    for (; m < 12U; m++) {
        const uint32_t len = mlen[m] + ((m == 1U && y % 4U == 0U) ? 1U : 0U);
        if (days < len) {
            break;
        }
        days -= len;
    }
    (void)snprintf(buf, n, "%04u-%02u-%02u %02lu:%02lu:%02lu", y, m + 1U, (unsigned)days + 1U,
                   (unsigned long)(rem / 3600UL), (unsigned long)(rem / 60UL % 60UL), (unsigned long)(rem % 60UL));
}

/* ---- ring ------------------------------------------------------------------- */

static void open_log(void)
{
    extflash_info_t fi;
    extflash_get_info(&fi);
    if (!fi.initialised && extflash_init() != ST_OK) {
        s_info.flash_ok = false;
        console_puts("[log] SPI flash not available: events go to the console only\r\n");
        return;
    }
    uint32_t newest = ERASED, newest_i = 0U, count = 0U;
    uint16_t boot = 0U;
    for (uint32_t i = 0; i < EVLOG_CAPACITY; i++) {
        evlog_rec_t r;
        if (!read_rec(i, &r) || r.seq == ERASED || r.check != rec_check(&r)) {
            continue;
        }
        count++;
        if (newest == ERASED || r.seq > newest) {
            newest = r.seq;
            newest_i = i;
            boot = r.boot;
        }
    }
    uint32_t next = (newest == ERASED) ? 0U : (newest_i + 1U) % EVLOG_CAPACITY;
    if ((slot_addr(next) % EXTFLASH_SECTOR) != 0U) {
        evlog_rec_t r;
        if (!read_rec(next, &r) || r.seq != ERASED) { /* torn write: continue in the next sector */
            next = ((slot_addr(next) / EXTFLASH_SECTOR + 1U) * EXTFLASH_SECTOR - EVLOG_BASE) / EVLOG_REC_SIZE;
            next %= EVLOG_CAPACITY;
        }
    }
    s_info.flash_ok = true;
    s_info.count = count;
    s_info.next = next;
    s_info.last_seq = (newest == ERASED) ? 0U : newest;
    s_info.boot = (uint16_t)(boot + 1U);
    console_printf("[log] event log: %lu records, next slot %lu, boot %u\r\n", (unsigned long)count,
                   (unsigned long)next, s_info.boot);
}

static void print_rec(const char *tag, const evlog_rec_t *r)
{
    char when[24];
    fmt_rtc(r->rtc_s, when, sizeof when);
    console_printf("[%s] seq=%lu t=%lu %s boot=%u %s mode=%s state=%s conf=%u a=%lu b=%lu\r\n", tag,
                   (unsigned long)r->seq, (unsigned long)r->t_ms, when, r->boot, evlog_type_name(r->type),
                   app_mode_name((app_mode_t)r->mode), r->state == 0xFFU ? "-" : model_class_name(r->state), r->conf,
                   (unsigned long)r->a, (unsigned long)r->b);
}

static void write_rec(evlog_rec_t *r)
{
    (void)xSemaphoreTake(s_mx, portMAX_DELAY);
    r->seq = ++s_info.last_seq;
    r->boot = s_info.boot;
    r->check = rec_check(r);
    bool ok = s_info.flash_ok;
    if (ok) {
        const uint32_t addr = slot_addr(s_info.next);
        if ((addr % EXTFLASH_SECTOR) == 0U) {
            ok = extflash_erase_sector(addr) == ST_OK;
            if (ok) { /* the oldest 128 records are gone */
                s_info.count = (s_info.count > EXTFLASH_SECTOR / EVLOG_REC_SIZE)
                                   ? s_info.count - EXTFLASH_SECTOR / EVLOG_REC_SIZE
                                   : 0U;
            }
        }
        ok = ok && extflash_program(addr, (const uint8_t *)r, EVLOG_REC_SIZE) == ST_OK;
        if (ok) {
            s_info.next = (s_info.next + 1U) % EVLOG_CAPACITY;
            s_info.count++;
            s_info.written++;
        }
    }
    if (!ok) {
        s_info.write_errors++;
    }
    xSemaphoreGive(s_mx);
    if (!g_console_quiet) {
        print_rec("event", r);
    }
}

static void log_task(void *arg)
{
    (void)arg;
    (void)xSemaphoreTake(s_mx, portMAX_DELAY);
    open_log();
    xSemaphoreGive(s_mx);
    for (;;) {
        wdg_checkin(WDG_LOG);
        evlog_rec_t r;
        if (xQueueReceive(s_q, &r, pdMS_TO_TICKS(1000)) == pdPASS) {
            r.rtc_s = rtc_seconds();
            write_rec(&r);
        }
    }
}

void evlog_post(ev_type_t type, uint8_t mode, uint8_t state, uint8_t conf, uint32_t a, uint32_t b)
{
    evlog_rec_t r;
    memset(&r, 0, sizeof r);
    r.t_ms = (uint32_t)xTaskGetTickCount();
    r.type = (uint8_t)type;
    r.mode = mode;
    r.state = state;
    r.conf = conf;
    r.a = a;
    r.b = b;
    infer_result_t ir;
    infer_get_last(&ir);
    if (ir.seq > 0U && ir.env_ok) {
        r.temp = (int16_t)(ir.temp * 100.0f);
        r.rh = (uint16_t)(ir.rh * 100.0f);
    }
    if (s_q == NULL || xQueueSend(s_q, &r, 0) != pdPASS) {
        s_dropped++;
    }
}

void evlog_get_info(evlog_info_t *out)
{
    (void)xSemaphoreTake(s_mx, portMAX_DELAY);
    *out = s_info;
    xSemaphoreGive(s_mx);
    out->dropped = s_dropped;
}

/* ---- console ------------------------------------------------------------------ */

static int cmd_log(int argc, char **argv)
{
    unsigned long n = 10U;
    if (argc > 1 || (argc == 1 && !cli_arg_u32(argv[0], 1U, 200U, &n))) {
        return 1;
    }
    evlog_info_t in;
    evlog_get_info(&in);
    console_printf("[log] flash %s, %lu records (capacity %lu), boot %u, written %lu, write errors %lu, "
                   "dropped %lu\r\n",
                   in.flash_ok ? "OK" : "NOT available", (unsigned long)in.count, (unsigned long)EVLOG_CAPACITY,
                   in.boot, (unsigned long)in.written, (unsigned long)in.write_errors, (unsigned long)in.dropped);
    if (!in.flash_ok) {
        return 0;
    }
    if (n > in.count) {
        n = in.count;
    }
    for (unsigned long k = n; k > 0U; k--) {
        const uint32_t i = (in.next + EVLOG_CAPACITY - k) % EVLOG_CAPACITY;
        evlog_rec_t r;
        (void)xSemaphoreTake(s_mx, portMAX_DELAY);
        const bool ok = read_rec(i, &r);
        xSemaphoreGive(s_mx);
        if (ok && r.seq != ERASED && r.check == rec_check(&r)) {
            print_rec("log", &r);
        }
    }
    return 0;
}

static int cmd_log_clear(int argc, char **argv)
{
    if (argc != 1 || strcmp(argv[0], "yes") != 0) {
        console_puts("usage: log clear yes\r\n");
        return 1;
    }
    status_t st = ST_OK;
    for (uint32_t addr = EVLOG_BASE; addr < EVLOG_BASE + EVLOG_SIZE && st == ST_OK; addr += EXTFLASH_SECTOR) {
        (void)xSemaphoreTake(s_mx, portMAX_DELAY); /* per sector, so LogTask is never held long */
        st = extflash_erase_sector(addr);
        s_info.count = 0U;
        s_info.next = 0U;
        xSemaphoreGive(s_mx);
    }
    console_printf("[log] erase %s\r\n", status_str(st));
    return st == ST_OK ? 0 : 1;
}

static const cli_cmd_t s_cmds[] = {
    {"log", "[n]", "event log status and the last n events (default 10)", cmd_log},
    {"log clear", "yes", "erase the event log", cmd_log_clear},
};

void evlog_task_create(void)
{
    s_q = xQueueCreateStatic(LOG_QUEUE_LEN, sizeof(evlog_rec_t), s_q_storage, &s_q_buf);
    s_mx = xSemaphoreCreateMutexStatic(&s_mx_buf);
    TaskHandle_t h = xTaskCreateStatic(log_task, "Log", LOG_TASK_STACK_WORDS, NULL, LOG_TASK_PRIO, s_stack, &s_tcb);
    configASSERT(s_q != NULL && s_mx != NULL && h != NULL);
    cli_register(s_cmds, sizeof s_cmds / sizeof s_cmds[0]);
}
