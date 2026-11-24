/*
 * rtc.h: STM32 internal RTC (calendar), clocked by the 32.768 kHz LSE
 * crystal X1 on PC14/PC15 (pinmap.md "Clocks"), with LSI as a logged
 * fallback if the LSE does not start (PRD 6.2 rtc.c).
 *
 * The RTC lives in the backup domain: it keeps running through a RESET-key
 * reset as long as the board has power. Our board has no coin cell in the
 * holder, so the time is lost when the power is removed.
 */
#ifndef RTC_H
#define RTC_H

#include <stdbool.h>
#include <stdint.h>

#include "status.h"

typedef enum {
    RTC_SRC_NONE = 0,
    RTC_SRC_LSE,
    RTC_SRC_LSI,
} rtc_source_t;

typedef struct {
    uint16_t year; /* 2000..2099 */
    uint8_t month, day, hour, minute, second;
    uint16_t ms;   /* from the sub-second counter, 1/256 s steps on LSE */
} rtc_datetime_t;

typedef struct {
    bool initialised;
    rtc_source_t source;
    bool kept_running;  /* the RTC was already running at init (survived the reset) */
    bool time_valid;    /* set with rtc_set() since the backup domain last lost power */
    uint32_t lse_start_ms; /* how long the LSE took to become ready (0 if kept running / LSI) */
} rtc_info_t;

/* Start the RTC (call from a task: waits up to 5 s for the LSE). If it is
   already running from the same clock it is left alone, so the time survives
   a RESET. */
status_t rtc_init(void);
bool rtc_is_initialised(void);
void rtc_get_info(rtc_info_t *info);
const char *rtc_source_name(rtc_source_t s);

status_t rtc_set(const rtc_datetime_t *dt);
status_t rtc_get(rtc_datetime_t *dt);

/* Compare the RTC against the CPU clock (HSE crystal): time `seconds` RTC
   second ticks with the DWT cycle counter. Returns the deviation of the RTC
   in ppm (positive = RTC fast) in *ppm_x10 (tenths of ppm). seconds <= 20
   (the 32-bit cycle counter wraps after 25.5 s). Blocks for seconds + ~2 s. */
status_t rtc_check_rate(unsigned seconds, int32_t *ppm_x10, uint32_t *measured_us);

#endif /* RTC_H */
