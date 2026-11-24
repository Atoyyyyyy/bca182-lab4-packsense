/*
 * wdg.h: independent watchdog with task check-ins (PRD 6.3 rules).
 *
 * Every monitored task calls wdg_checkin() at least once per WDG_WINDOW_MS,
 * which sets its bit in an event group. The heartbeat task calls
 * wdg_service() every 500 ms: at the end of each window it checks that all
 * bits were set, clears them, and keeps feeding the IWDG only while the last
 * window was complete. A stuck task therefore resets the board within
 * WDG_WINDOW_MS + WDG_TIMEOUT_MS. The missing tasks are saved in an RTC
 * backup register and printed after the reset.
 */
#ifndef WDG_H
#define WDG_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    WDG_SENSOR = 0,
    WDG_ENV,
    WDG_INFER,
    WDG_UI,
    WDG_LOG,
    WDG_CAPTURE,
    WDG_HEARTBEAT,
    WDG_COUNT
} wdg_id_t;

typedef struct {
    bool running;
    uint32_t windows_ok;
    uint32_t windows_missed;
    uint32_t last_missing;          /* bit mask of the last incomplete window */
    uint32_t age_ms[WDG_COUNT];     /* time since each task's last check-in */
} wdg_status_t;

/* Before the scheduler: create the event group. reset_csr = RCC->CSR at boot
   (prints which tasks were late if the last reset was the watchdog). */
void wdg_init(uint32_t reset_csr);

/* Start the IWDG (cannot be stopped again). Frozen while a debugger halts the core. */
void wdg_start(void);

void wdg_checkin(wdg_id_t id);
void wdg_service(void);
void wdg_get_status(wdg_status_t *out);
const char *wdg_name(wdg_id_t id);

/* `fault hang <task>`: that task stops checking in (to show the watchdog). */
bool wdg_hang_requested(wdg_id_t id);
void wdg_request_hang(wdg_id_t id);

/* true when the last reset was this watchdog (for the event log). */
bool wdg_was_reset(void);
uint32_t wdg_prev_missing(void);

#endif /* WDG_H */
