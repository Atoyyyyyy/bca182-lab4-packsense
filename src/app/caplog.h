/*
 * caplog.h: capture log in the SPI flash (M3). The kit has no microSD card
 * (exceptions.md #1), so labelled trials are recorded to the 8 MB SPI flash
 * and downloaded later over the console (tools/capture.py).
 *
 * Region 0x010000..0x7EFFFF (8,257,536 B = 258,048 records = 43 min at
 * 100 Hz). 0x000000..0x00FFFF stays free for settings / event log (M5); the
 * last 64 KB holds the flash self-test sector.
 *
 * Records are written in order from the start of the region; everything
 * after the last record is erased (caplog_wipe erases the used extent; the
 * writer erases a sector before entering it if it is not blank). The end is
 * found at boot by binary search for the first erased record.
 *
 * Record (32 bytes, little-endian), scaled integers:
 *   u32 t_ms | i16 ax,ay,az (m/s^2 x 800) | i16 gx,gy,gz (deg/s x 60)
 *   u16 als (lux / 0.35, the AP3216C count) | u16 ps (10-bit)
 *   i16 temp (degC x 100) | u16 rh (%RH x 100)
 *   u8 label | u8 flags (SF_*, bit 6 = ps_valid) | u16 trial | u16 session
 *   u16 check = 0xA5A5 ^ (16-bit sum of the 30 bytes before it)
 */
#ifndef CAPLOG_H
#define CAPLOG_H

#include <stdbool.h>
#include <stdint.h>

#include "app/sensor_task.h"
#include "status.h"

#define CAPLOG_BASE     0x010000UL
#define CAPLOG_END      0x7F0000UL
#define CAPLOG_REC_SIZE 32U
#define CAPLOG_CAPACITY ((CAPLOG_END - CAPLOG_BASE) / CAPLOG_REC_SIZE)
#define CAPLOG_FLAG_PS_VALID 0x40U

#define CAPLOG_ACC_SCALE  800.0f
#define CAPLOG_GYR_SCALE  60.0f
#define CAPLOG_ALS_LSB    0.35f

typedef struct __attribute__((packed)) {
    uint32_t t_ms;
    int16_t a[3];
    int16_t g[3];
    uint16_t als;
    uint16_t ps;
    int16_t temp;
    uint16_t rh;
    uint8_t label;
    uint8_t flags;
    uint16_t trial;
    uint16_t session;
    uint16_t check;
} caplog_rec_t;

typedef struct {
    bool open;
    uint32_t records;       /* records stored */
    uint32_t capacity;
    uint16_t last_session;  /* session of the last stored record (0 = empty) */
    uint32_t write_errors;
    uint32_t erases;
} caplog_info_t;

/* Init the flash and find the end of the log. */
status_t caplog_open(void);
void caplog_get_info(caplog_info_t *out);

/* Convert a sample to a record (fills check). */
void caplog_pack(const sample_t *s, uint8_t label, uint16_t trial, uint16_t session, caplog_rec_t *r);
bool caplog_check_ok(const caplog_rec_t *r);

/* Buffer one record; programs a page when 8 are buffered. ST_RANGE_ERR when
   the region is full. May block ~60 ms when it has to erase a sector. */
status_t caplog_append(const caplog_rec_t *r);
/* Program any buffered records (end of trial). */
status_t caplog_flush(void);

status_t caplog_read(uint32_t index, caplog_rec_t *r, uint32_t n);

/* Erase the used part of the region (64 KB blocks). */
status_t caplog_wipe(void);

#endif /* CAPLOG_H */
