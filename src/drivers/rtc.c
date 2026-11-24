/*
 * rtc.c: internal RTC calendar on LSE (32.768 kHz crystal X1), LSI fallback.
 *
 * LSE: asynchronous prescaler 128, synchronous 256 -> 1 Hz calendar,
 * sub-seconds in 1/256 s. LSI (~32 kHz RC, only if the LSE fails): 128 x 250,
 * nominal 1 Hz but up to several percent off; rtc_info_t says which one runs.
 *
 * Backup register 0 holds a marker once the time has been set, so after a
 * RESET we can tell "kept running with a valid time" from "running since
 * power-up from 2000-01-01".
 */
#include "rtc.h"

#include "FreeRTOS.h"
#include "task.h"

#include "stm32f4xx_hal.h"

#define TIME_SET_MARKER 0x50AC5E7DUL /* in RTC->BKP0R */
#define LSE_ASYNC 127U
#define LSE_SYNC  255U
#define LSI_ASYNC 127U
#define LSI_SYNC  249U

static RTC_HandleTypeDef s_rtc;
static rtc_info_t s_info;

const char *rtc_source_name(rtc_source_t s)
{
    switch (s) {
    case RTC_SRC_LSE: return "LSE";
    case RTC_SRC_LSI: return "LSI";
    default:          return "none";
    }
}

static void fill_handle(rtc_source_t src)
{
    s_rtc.Instance = RTC;
    s_rtc.Init.HourFormat = RTC_HOURFORMAT_24;
    s_rtc.Init.AsynchPrediv = (src == RTC_SRC_LSI) ? LSI_ASYNC : LSE_ASYNC;
    s_rtc.Init.SynchPrediv = (src == RTC_SRC_LSI) ? LSI_SYNC : LSE_SYNC;
    s_rtc.Init.OutPut = RTC_OUTPUT_DISABLE;
    s_rtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
    s_rtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
}

status_t rtc_init(void)
{
    s_info = (rtc_info_t){0};

    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();

    /* Already running on the LSE (backup domain survived a RESET)? Then keep
       it: re-initialising would stop the calendar for a moment. */
    const uint32_t bdcr = RCC->BDCR;
    const bool on_lse = (bdcr & RCC_BDCR_RTCSEL) == RCC_BDCR_RTCSEL_0;
    if ((bdcr & RCC_BDCR_RTCEN) && on_lse && (bdcr & RCC_BDCR_LSERDY) && (RTC->ISR & RTC_ISR_INITS)) {
        fill_handle(RTC_SRC_LSE);
        s_rtc.State = HAL_RTC_STATE_READY;
        s_rtc.Lock = HAL_UNLOCKED;
        if (HAL_RTC_WaitForSynchro(&s_rtc) != HAL_OK) {
            return ST_TIMEOUT;
        }
        s_info.source = RTC_SRC_LSE;
        s_info.kept_running = true;
        s_info.time_valid = RTC->BKP0R == TIME_SET_MARKER;
        s_info.initialised = true;
        return ST_OK;
    }

    /* Start the LSE (HAL waits up to LSE_STARTUP_TIMEOUT = 5 s). */
    rtc_source_t src = RTC_SRC_LSE;
    RCC_OscInitTypeDef osc = {0};
    osc.OscillatorType = RCC_OSCILLATORTYPE_LSE;
    osc.LSEState = RCC_LSE_ON;
    osc.PLL.PLLState = RCC_PLL_NONE;
    const TickType_t t0 = xTaskGetTickCount();
    if (HAL_RCC_OscConfig(&osc) == HAL_OK) {
        s_info.lse_start_ms = (uint32_t)(xTaskGetTickCount() - t0);
    } else {
        src = RTC_SRC_LSI;
        osc.OscillatorType = RCC_OSCILLATORTYPE_LSI;
        osc.LSEState = RCC_LSE_OFF;
        osc.LSIState = RCC_LSI_ON;
        if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
            return ST_TIMEOUT;
        }
    }

    /* Select the RTC clock. HAL resets the backup domain if the selection
       changes (that clears the calendar and BKP0R). */
    RCC_PeriphCLKInitTypeDef pc = {0};
    pc.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    pc.RTCClockSelection = (src == RTC_SRC_LSE) ? RCC_RTCCLKSOURCE_LSE : RCC_RTCCLKSOURCE_LSI;
    if (HAL_RCCEx_PeriphCLKConfig(&pc) != HAL_OK) {
        return ST_BUS_ERR;
    }
    __HAL_RCC_RTC_ENABLE();

    fill_handle(src);
    s_rtc.State = HAL_RTC_STATE_RESET;
    if (HAL_RTC_Init(&s_rtc) != HAL_OK) {
        return ST_TIMEOUT;
    }
    RTC->BKP0R = 0U; /* calendar is not set yet */

    s_info.source = src;
    s_info.initialised = true;
    return ST_OK;
}

bool rtc_is_initialised(void)
{
    return s_info.initialised;
}

void rtc_get_info(rtc_info_t *info)
{
    if (s_info.initialised) {
        s_info.time_valid = RTC->BKP0R == TIME_SET_MARKER;
    }
    *info = s_info;
}

/* Day of the week, 1 = Monday .. 7 = Sunday (RTC_WEEKDAY_*). */
static uint8_t weekday(unsigned y, unsigned m, unsigned d)
{
    static const uint8_t t[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3U) {
        y -= 1U;
    }
    const unsigned dow = (y + y / 4U - y / 100U + y / 400U + t[m - 1U] + d) % 7U; /* 0 = Sunday */
    return (uint8_t)(dow == 0U ? 7U : dow);
}

static unsigned days_in_month(unsigned y, unsigned m)
{
    static const uint8_t dim[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (y % 4U == 0U && y % 100U != 0U) || (y % 400U == 0U);
    return (m == 2U && leap) ? 29U : dim[m - 1U];
}

status_t rtc_set(const rtc_datetime_t *dt)
{
    if (!s_info.initialised) {
        return ST_NOT_PRESENT;
    }
    if (dt->year < 2000U || dt->year > 2099U || dt->month < 1U || dt->month > 12U || dt->day < 1U ||
        dt->day > days_in_month(dt->year, dt->month) || dt->hour > 23U || dt->minute > 59U || dt->second > 59U) {
        return ST_RANGE_ERR;
    }
    RTC_TimeTypeDef t = {0};
    t.Hours = dt->hour;
    t.Minutes = dt->minute;
    t.Seconds = dt->second;
    t.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    t.StoreOperation = RTC_STOREOPERATION_RESET;
    RTC_DateTypeDef d = {0};
    d.Year = (uint8_t)(dt->year - 2000U);
    d.Month = dt->month;
    d.Date = dt->day;
    d.WeekDay = weekday(dt->year, dt->month, dt->day);
    if (HAL_RTC_SetDate(&s_rtc, &d, RTC_FORMAT_BIN) != HAL_OK || HAL_RTC_SetTime(&s_rtc, &t, RTC_FORMAT_BIN) != HAL_OK) {
        return ST_TIMEOUT;
    }
    RTC->BKP0R = TIME_SET_MARKER;
    return ST_OK;
}

status_t rtc_get(rtc_datetime_t *dt)
{
    if (!s_info.initialised) {
        return ST_NOT_PRESENT;
    }
    RTC_TimeTypeDef t;
    RTC_DateTypeDef d;
    /* Time first, then date: reading the time locks the shadow date. */
    if (HAL_RTC_GetTime(&s_rtc, &t, RTC_FORMAT_BIN) != HAL_OK || HAL_RTC_GetDate(&s_rtc, &d, RTC_FORMAT_BIN) != HAL_OK) {
        return ST_TIMEOUT;
    }
    dt->year = (uint16_t)(2000U + d.Year);
    dt->month = d.Month;
    dt->day = d.Date;
    dt->hour = t.Hours;
    dt->minute = t.Minutes;
    dt->second = t.Seconds;
    dt->ms = (uint16_t)(((t.SecondFraction - t.SubSeconds) * 1000U) / (t.SecondFraction + 1U));
    return ST_OK;
}

/* ---- rate check ----------------------------------------------------------- */

/* Calendar seconds 0..59 (read TR then DR to release the shadow lock). */
static unsigned sec_now(void)
{
    const uint32_t tr = RTC->TR;
    (void)RTC->DR;
    return ((tr >> 4) & 0x7U) * 10U + (tr & 0xFU);
}

/* Coarse: wait (sleeping 1 tick at a time) until the seconds change. */
static bool coarse_edge(uint32_t *cyc, uint32_t timeout_ms)
{
    const unsigned s = sec_now();
    const TickType_t t0 = xTaskGetTickCount();
    while (sec_now() == s) {
        if ((xTaskGetTickCount() - t0) > pdMS_TO_TICKS(timeout_ms)) {
            return false;
        }
        vTaskDelay(1);
    }
    *cyc = DWT->CYCCNT;
    return true;
}

/* Fine: sleep until `margin_cyc` before the predicted edge, check that the
   edge has not passed (seconds still `before`), then spin on the register. */
static status_t fine_edge(uint32_t from, uint32_t predicted, uint32_t margin_cyc, unsigned before, uint32_t *cyc)
{
    const uint32_t per_ms = SystemCoreClock / 1000U;
    /* All times as unsigned offsets from `from` (< 25.5 s, so no wrap). A
       signed difference would overflow beyond 12.8 s (m2_rtc.log, c4464ec). */
    const uint32_t wake_off = (predicted - from) - margin_cyc;
    const uint32_t now_off = DWT->CYCCNT - from;
    if (now_off < wake_off) {
        vTaskDelay(pdMS_TO_TICKS((wake_off - now_off) / per_ms));
    }
    if (sec_now() != before) {
        return ST_RANGE_ERR; /* woke too late: prediction off */
    }
    const uint32_t limit = (predicted - from) + 2U * margin_cyc;
    while (sec_now() == before) {
        if ((DWT->CYCCNT - from) > limit) {
            return ST_TIMEOUT;
        }
    }
    *cyc = DWT->CYCCNT;
    return ST_OK;
}

status_t rtc_check_rate(unsigned seconds, int32_t *ppm_x10, uint32_t *measured_us)
{
    if (!s_info.initialised) {
        return ST_NOT_PRESENT;
    }
    if (seconds < 1U || seconds > 20U) {
        return ST_RANGE_ERR;
    }
    const uint32_t per_ms = SystemCoreClock / 1000U;

    /* Two coarse edges (+-1 ms) give the length of one RTC second. */
    uint32_t e0, e1;
    if (!coarse_edge(&e0, 2000U) || !coarse_edge(&e1, 2000U)) {
        return ST_TIMEOUT;
    }
    const uint32_t period = e1 - e0;

    /* Precise start edge, then precise end edge `seconds` later. The margin
       covers the +-2 ms error of `period` times the number of seconds. */
    uint32_t c_start, c_end;
    unsigned before = sec_now();
    status_t st = fine_edge(e1, e1 + period, 10U * per_ms, before, &c_start);
    if (st != ST_OK) {
        return st;
    }
    const unsigned start_sec = sec_now();
    before = (start_sec + seconds - 1U) % 60U;
    st = fine_edge(c_start, c_start + seconds * period, (10U + 3U * seconds) * per_ms, before, &c_end);
    if (st != ST_OK) {
        return st;
    }

    const uint32_t measured = c_end - c_start;
    const int64_t expected = (int64_t)seconds * SystemCoreClock;
    /* RTC fast -> its seconds are short -> fewer CPU cycles -> positive. */
    *ppm_x10 = (int32_t)(((expected - (int64_t)measured) * 10000000LL) / (int64_t)measured);
    *measured_us = (uint32_t)((uint64_t)measured * 1000000ULL / SystemCoreClock);
    return ST_OK;
}
