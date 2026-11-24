/*
 * rw007.h: RW007 Wi-Fi module on the shared SPI2 bus (CS1 PF10, INT PG11,
 * RST PG15). Control commands only: reset, init, station mode, serial
 * number, firmware version, MAC and a scan. No join and no IP stack in M2
 * (Wi-Fi is the optional comms path, PRD inventory row 12).
 *
 * SPI framing (two-stage master request / slave response with magic words,
 * then a data phase) and the command numbers follow the board vendor's
 * rw007 package v2.0.1 (spi_wifi_rw007.c/.h, Apache-2.0), rewritten here
 * as a small synchronous driver for FreeRTOS.
 */
#ifndef RW007_H
#define RW007_H

#include <stdbool.h>
#include <stdint.h>

#include "status.h"

typedef struct {
    bool ready;
    uint32_t boot_ms;      /* RST high -> INT high (module out of busy) */
    char sn[25];
    char version[17];
    uint8_t mac[6];
    uint32_t init_ms;      /* INIT + SET_MODE + SN + VERSION + MAC */
} rw007_info_t;

typedef struct {
    uint32_t transfers;
    uint32_t stage1_err;
    uint32_t stage2_err;
    uint32_t int_timeouts; /* INT edge not seen within 100 ms after a stage */
    uint32_t retries;
    uint32_t rx_packets;
    uint32_t unsolicited;  /* packets that were not the awaited response */
    uint32_t int_edges;
} rw007_stats_t;

#define RW007_SCAN_MAX 24U

typedef struct {
    int16_t rssi;
    int16_t channel;
    uint32_t security;   /* rt_wlan_security_t value as reported */
    uint8_t ssid_len;    /* the name itself is not kept */
    uint8_t bssid[6];
} rw007_ap_t;

typedef struct {
    unsigned count;      /* reports received (may exceed RW007_SCAN_MAX) */
    bool done;           /* SCAN_DONE event seen */
    uint32_t ms;
    rw007_ap_t ap[RW007_SCAN_MAX];
} rw007_scan_t;

/* Hardware reset, wait for the module, then INIT, station mode, SN,
   version and MAC. Calls spi2_bus_init(). Call from a task. */
status_t rw007_init(rw007_info_t *info);
bool rw007_is_ready(void);

/* Start a scan and collect reports until SCAN_DONE or timeout_ms. */
status_t rw007_scan(uint32_t timeout_ms, rw007_scan_t *out);

void rw007_get_stats(rw007_stats_t *s);

/* EXTI15_10 handler hook (PG11 falling edge). */
void rw007_exti_irq(void);

#endif /* RW007_H */
