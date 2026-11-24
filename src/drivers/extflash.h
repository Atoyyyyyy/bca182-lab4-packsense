/*
 * extflash.h: W25Q-series SPI NOR flash on SPI2 (CS0 = PB12).
 * Pins: include/board_pins.h (pinmap.md "Storage").
 *
 * Only standard SPI-NOR commands are used (JEDEC ID 0x9F, SFDP 0x5A, read
 * 0x03, write enable 0x06, page program 0x02, 4 KB sector erase 0x20, status
 * 0x05, 64 KB block erase 0xD8), and the capacity is taken from the chip's own SFDP table (JESD216),
 * so nothing depends on the exact part number.
 *
 * SPI2 is shared with the RW007 Wi-Fi module (spi2_bus.c holds the mutex).
 * Callers that write must serialise among themselves (one owner per region:
 * the capture log owns 0x010000..0x7EFFFF, see caplog.h).
 */
#ifndef EXTFLASH_H
#define EXTFLASH_H

#include <stdbool.h>
#include <stdint.h>

#include "status.h"

typedef struct {
    bool initialised;
    uint8_t jedec[3];      /* manufacturer, memory type, capacity code */
    bool sfdp_ok;          /* "SFDP" signature found */
    uint8_t sfdp_major, sfdp_minor;
    uint32_t size_bytes;   /* from SFDP density (0 if no SFDP) */
    uint32_t spi_hz;
} extflash_info_t;

typedef struct {
    uint32_t addr;         /* sector tested (the last 4 KB of the chip) */
    bool was_blank;        /* sector was all 0xFF before the test */
    uint32_t erase_ms;     /* first erase */
    uint32_t program_us;   /* 16 pages x 256 B */
    unsigned erase_errors; /* bytes not 0xFF after erase */
    unsigned verify_errors;/* bytes differing from the pattern */
    unsigned restore_errors;/* bytes differing from the saved content after restore */
} extflash_test_t;

/* Chip selects high, SPI2 set up, wake the chip, read JEDEC ID + SFDP.
   ST_NOT_PRESENT if the JEDEC ID reads 00 00 00 or FF FF FF. */
status_t extflash_init(void);
void extflash_get_info(extflash_info_t *info);

status_t extflash_read(uint32_t addr, uint8_t *buf, uint32_t len);

#define EXTFLASH_SECTOR 4096U
#define EXTFLASH_BLOCK  65536U
#define EXTFLASH_PAGE   256U

/* Erase one 4 KB sector / one 64 KB block (address rounded down). Sleep-waits
   (vTaskDelay) for completion: ~60 ms / ~150-300 ms on this chip. */
status_t extflash_erase_sector(uint32_t addr);
status_t extflash_erase_block(uint32_t addr);

/* Program up to one page; must not cross a 256-byte page boundary. */
status_t extflash_program(uint32_t addr, const uint8_t *data, uint32_t len);

/* Non-destructive write test on the last 4 KB sector: save it, erase,
   program a pattern, verify, then erase and write the saved bytes back. */
status_t extflash_selftest(extflash_test_t *res);

#endif /* EXTFLASH_H */
