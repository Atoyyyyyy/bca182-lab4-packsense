/*
 * evlog.h: event log + LogTask (PRD 6.3, priority 1).
 *
 * Events (state changes, alerts, faults, mode changes, boots) go into a ring
 * of 2048 x 32-byte records in the SPI flash at 0x000000..0x00FFFF (the
 * capture log starts at 0x010000), each with a sequence number, uptime, RTC
 * time (when set) and a checksum, and are echoed on the console as
 * `[event] ...` lines. Only state and time are logged, never raw sensor data
 * (PRD 3, privacy). If the flash is not usable the console line is the
 * fallback and the loss is counted.
 */
#ifndef EVLOG_H
#define EVLOG_H

#include <stdbool.h>
#include <stdint.h>

#define EVLOG_BASE      0x000000UL
#define EVLOG_SIZE      0x010000UL
#define EVLOG_REC_SIZE  32U
#define EVLOG_CAPACITY  (EVLOG_SIZE / EVLOG_REC_SIZE)

typedef enum {
    EV_BOOT = 1,    /* a = RCC_CSR at reset, b = boot self-test failure mask */
    EV_MODE,        /* a = new mode, b = previous mode */
    EV_STATE,       /* state/conf = new smoothed state, a = previous state */
    EV_ROUGH,       /* rough handling alert */
    EV_ACCESS,      /* a = 1 when guarded (alarm), 0 = logged only */
    EV_ACK,         /* alarm acknowledged, a = ms from alarm start, b = key */
    EV_FAULT,       /* a = fault mask now, b = previous mask */
    EV_CLIMATE,     /* a = 1 warning on, 0 off */
    EV_WDG,         /* last reset was the watchdog, a = tasks that missed the check-in */
    EV_TYPE_COUNT
} ev_type_t;

typedef struct __attribute__((packed)) {
    uint32_t seq;   /* 0xFFFFFFFF = erased slot */
    uint32_t t_ms;  /* uptime */
    uint32_t rtc_s; /* seconds since 2000-01-01 00:00:00, 0 = RTC not set */
    uint8_t type;   /* ev_type_t */
    uint8_t mode;   /* app_mode_t */
    uint8_t state;  /* CLS_*, 0xFF = none */
    uint8_t conf;   /* % */
    int16_t temp;   /* deg C x 100 */
    uint16_t rh;    /* %RH x 100 */
    uint32_t a, b;
    uint16_t boot;  /* boot counter */
    uint16_t check; /* 0x5A5A ^ 16-bit sum of the first 30 bytes */
} evlog_rec_t;

typedef struct {
    bool flash_ok;
    uint32_t count;        /* records in the ring */
    uint32_t next;         /* slot index of the next write */
    uint32_t last_seq;
    uint16_t boot;
    uint32_t written, write_errors, dropped;
} evlog_info_t;

/* Create LogTask (opens the log from the task). Before the scheduler. */
void evlog_task_create(void);

/* Queue an event (non-blocking; counted as dropped if the queue is full).
   state 0xFF = none; temp/rh are filled in from the last inference result. */
void evlog_post(ev_type_t type, uint8_t mode, uint8_t state, uint8_t conf, uint32_t a, uint32_t b);

void evlog_get_info(evlog_info_t *out);
const char *evlog_type_name(uint8_t type);

#endif /* EVLOG_H */
