/*
 * sdcard.h: microSD over SDIO (4-bit), polled transfers.
 * Pins: include/board_pins.h (pinmap.md "Storage").
 *
 * M2 scope: detect, initialise, report the card and read sector 0. The FAT
 * filesystem (PRD 6.2 sdlog.c) comes with the logging task.
 */
#ifndef SDCARD_H
#define SDCARD_H

#include <stdbool.h>
#include <stdint.h>

#include "status.h"

typedef struct {
    bool present;           /* SDIO init succeeded */
    bool cd_pin_high;       /* raw level of the card-detect pin PF3 */
    uint32_t hal_error;     /* HAL_SD error code of the last failure (0 = none) */
    uint32_t card_type;     /* 0 = SDSC, 1 = SDHC/SDXC (HAL CARD_SDSC / CARD_SDHC_SDXC) */
    uint32_t block_count;   /* 512-byte blocks */
    uint8_t manufacturer;   /* CID MID */
    char oem[3];            /* CID OID, 2 chars */
    char product[6];        /* CID PNM, 5 chars */
    uint32_t serial;        /* CID PSN */
    uint32_t bus_hz;        /* SDIO clock after init */
} sd_info_t;

/* Card-detect pin input (pull-up). Safe at boot. */
void sd_early_init(void);
bool sd_cd_pin_high(void);

/* Try to initialise a card: ST_NOT_PRESENT if none answers. */
status_t sd_init(void);
void sd_get_info(sd_info_t *info);

/* Read one 512-byte block (polled). */
status_t sd_read_block(uint32_t lba, uint8_t buf[512]);

#endif /* SDCARD_H */
