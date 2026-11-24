/*
 * wdg.c: IWDG + check-in event group (see wdg.h).
 */
#include "wdg.h"

#include "stm32f4xx_hal.h"

#include "FreeRTOS.h"
#include "event_groups.h"
#include "task.h"

#include "app_config.h"
#include "drivers/console_uart.h"

#define LSI_HZ        32000U
#define IWDG_DIV      64U                                  /* PR = 4 */
#define IWDG_RELOAD   ((WDG_TIMEOUT_MS * (LSI_HZ / IWDG_DIV)) / 1000U)
#define BKP_MAGIC     0x5D000000UL                         /* RTC->BKP19R: magic | missing mask */
#define REQUIRED_MASK ((1UL << WDG_COUNT) - 1UL)

static StaticEventGroup_t s_eg_buf;
static EventGroupHandle_t s_eg;
static volatile bool s_running;
static volatile bool s_window_ok = true;
static TickType_t s_window_t0;
static volatile TickType_t s_last[WDG_COUNT];
static volatile uint32_t s_hang;
static uint32_t s_ok, s_missed, s_last_missing;
static bool s_reported;
static bool s_was_reset;
static uint32_t s_prev_missing;

static const char *const k_names[WDG_COUNT] = {"Sensor", "Env", "Infer", "Ui", "Log", "Capture", "Heartbeat"};

const char *wdg_name(wdg_id_t id)
{
    return id < WDG_COUNT ? k_names[id] : "?";
}

static void print_mask(const char *prefix, uint32_t mask)
{
    console_printf("%s", prefix);
    for (unsigned i = 0; i < WDG_COUNT; i++) {
        if (mask & (1UL << i)) {
            console_printf(" %s", k_names[i]);
        }
    }
    console_puts("\r\n");
}

void wdg_init(uint32_t reset_csr)
{
    s_eg = xEventGroupCreateStatic(&s_eg_buf);
    configASSERT(s_eg != NULL);
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    const uint32_t bkp = RTC->BKP19R;
    if ((reset_csr & RCC_CSR_IWDGRSTF) != 0U) {
        s_was_reset = true;
        if ((bkp & 0xFF000000UL) == BKP_MAGIC) {
            s_prev_missing = bkp & 0x00FFFFFFUL;
            print_mask("[wdg] the last reset was the watchdog; tasks that missed their check-in:", s_prev_missing);
        } else {
            console_puts("[wdg] the last reset was the watchdog (no task recorded)\r\n");
        }
    }
    RTC->BKP19R = 0U;
}

void wdg_start(void)
{
    if (s_running) {
        return;
    }
    DBGMCU->APB1FZ |= DBGMCU_APB1_FZ_DBG_IWDG_STOP;
    IWDG->KR = 0xCCCCU; /* start (LSI turns on) */
    IWDG->KR = 0x5555U; /* unlock PR/RLR */
    IWDG->PR = 4U;      /* /64 */
    IWDG->RLR = IWDG_RELOAD - 1U;
    const TickType_t t0 = xTaskGetTickCount();
    while (IWDG->SR != 0U && (xTaskGetTickCount() - t0) < pdMS_TO_TICKS(100)) {
    }
    IWDG->KR = 0xAAAAU;
    const TickType_t now = xTaskGetTickCount();
    for (unsigned i = 0; i < WDG_COUNT; i++) {
        s_last[i] = now;
    }
    (void)xEventGroupClearBits(s_eg, REQUIRED_MASK);
    s_window_t0 = now;
    s_window_ok = true;
    s_running = true;
    console_printf("[wdg] t=%lu IWDG started: timeout %u ms, check-in window %u ms, %u tasks\r\n",
                   (unsigned long)now, WDG_TIMEOUT_MS, WDG_WINDOW_MS, (unsigned)WDG_COUNT);
}

void wdg_checkin(wdg_id_t id)
{
    if (id >= WDG_COUNT || (s_hang & (1UL << id)) != 0U || s_eg == NULL) {
        return;
    }
    s_last[id] = xTaskGetTickCount();
    (void)xEventGroupSetBits(s_eg, 1UL << id);
}

void wdg_service(void)
{
    wdg_checkin(WDG_HEARTBEAT);
    if (!s_running) {
        return;
    }
    const TickType_t now = xTaskGetTickCount();
    if ((now - s_window_t0) >= pdMS_TO_TICKS(WDG_WINDOW_MS)) {
        const uint32_t bits = (uint32_t)xEventGroupGetBits(s_eg) & REQUIRED_MASK;
        if (bits == REQUIRED_MASK) {
            s_ok++;
            s_window_ok = true;
            (void)xEventGroupClearBits(s_eg, REQUIRED_MASK);
            wdg_checkin(WDG_HEARTBEAT); /* this call counts for the new window */
        } else {
            s_missed++;
            s_window_ok = false;
            s_last_missing = REQUIRED_MASK & ~bits;
            RTC->BKP19R = BKP_MAGIC | s_last_missing;
            if (!s_reported) {
                s_reported = true;
                console_printf("[wdg] t=%lu", (unsigned long)now);
                print_mask(" check-in missing, IWDG no longer fed:", s_last_missing);
            }
        }
        s_window_t0 = now;
    }
    if (s_window_ok) {
        IWDG->KR = 0xAAAAU;
    }
}

void wdg_get_status(wdg_status_t *out)
{
    const TickType_t now = xTaskGetTickCount();
    out->running = s_running;
    out->windows_ok = s_ok;
    out->windows_missed = s_missed;
    out->last_missing = s_last_missing;
    for (unsigned i = 0; i < WDG_COUNT; i++) {
        out->age_ms[i] = (uint32_t)(now - s_last[i]);
    }
}

bool wdg_hang_requested(wdg_id_t id)
{
    return id < WDG_COUNT && (s_hang & (1UL << id)) != 0U;
}

void wdg_request_hang(wdg_id_t id)
{
    if (id < WDG_COUNT) {
        s_hang |= 1UL << id;
    }
}

bool wdg_was_reset(void)
{
    return s_was_reset;
}

uint32_t wdg_prev_missing(void)
{
    return s_prev_missing;
}
