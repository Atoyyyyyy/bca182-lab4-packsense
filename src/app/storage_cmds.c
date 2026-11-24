/*
 * storage_cmds.c: console commands for the storage devices.
 *
 *   sd          card-detect pin, SDIO init, card info, read-only sector-0 check
 *   flash       SPI flash JEDEC ID, SFDP capacity
 *   flash test  non-destructive write test on the last 4 KB sector
 *
 * Nothing here writes to the SD card. `flash test` saves the sector it uses
 * and writes it back afterwards.
 */
#include "storage_cmds.h"

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include "app/cli.h"
#include "drivers/console_uart.h"
#include "drivers/extflash.h"
#include "drivers/sdcard.h"

#define T_NOW() ((unsigned long)xTaskGetTickCount())

static uint8_t s_block[512];

static int cmd_sd(int argc, char **argv)
{
    (void)argv;
    if (argc != 0) {
        return 1;
    }
    console_printf("[sd] t=%lu card-detect pin PF3 = %s (polarity not confirmed yet)\r\n", T_NOW(),
                   sd_cd_pin_high() ? "high" : "low");
    const TickType_t t0 = xTaskGetTickCount();
    status_t st = sd_init();
    sd_info_t i;
    sd_get_info(&i);
    console_printf("[sd] t=%lu init %s in %lu ms hal_error=0x%08lX\r\n", T_NOW(), status_str(st),
                   (unsigned long)(xTaskGetTickCount() - t0), (unsigned long)i.hal_error);
    if (st == ST_NOT_PRESENT) {
        console_puts("[sd] no card answered on SDIO (none inserted, or not seated)\r\n");
        return 0;
    }
    if (st != ST_OK) {
        return 0;
    }
    const unsigned long mib = (unsigned long)(i.block_count / 2048U);
    console_printf("[sd] type=%s blocks=%lu (%lu MiB) bus=4-bit %lu Hz\r\n",
                   i.card_type == 0U ? "SDSC" : "SDHC/SDXC", (unsigned long)i.block_count, mib,
                   (unsigned long)i.bus_hz);
    console_printf("[sd] CID: MID=0x%02X OID=%s PNM=%s PSN=0x%08lX\r\n", i.manufacturer, i.oem, i.product,
                   (unsigned long)i.serial);

    st = sd_read_block(0U, s_block);
    if (st != ST_OK) {
        console_printf("[sd] t=%lu read sector 0: %s\r\n", T_NOW(), status_str(st));
        return 0;
    }
    const bool sig = s_block[510] == 0x55U && s_block[511] == 0xAAU;
    /* Partition 1 type byte (MBR offset 0x1C2); 0x0B/0x0C = FAT32, 0x07 = exFAT/NTFS. */
    console_printf("[sd] t=%lu sector 0 read OK: signature %02X %02X (%s), partition-1 type 0x%02X\r\n", T_NOW(),
                   s_block[510], s_block[511], sig ? "valid MBR/boot sector" : "no 55 AA", s_block[0x1C2]);
    return 0;
}

static bool flash_ready(void)
{
    extflash_info_t i;
    extflash_get_info(&i);
    if (i.initialised) {
        return true;
    }
    status_t st = extflash_init();
    extflash_get_info(&i);
    console_printf("[flash] t=%lu init %s: JEDEC ID %02X %02X %02X, SPI2 %lu Hz\r\n", T_NOW(), status_str(st),
                   i.jedec[0], i.jedec[1], i.jedec[2], (unsigned long)i.spi_hz);
    if (st == ST_OK) {
        if (i.sfdp_ok) {
            console_printf("[flash] SFDP v%u.%u: density %lu bytes (%lu MiB)\r\n", i.sfdp_major, i.sfdp_minor,
                           (unsigned long)i.size_bytes, (unsigned long)(i.size_bytes >> 20));
        } else {
            console_puts("[flash] no SFDP table: capacity unknown, flash test disabled\r\n");
        }
    }
    return st == ST_OK;
}

static int cmd_flash(int argc, char **argv)
{
    (void)argv;
    if (argc != 0) {
        return 1;
    }
    if (flash_ready()) {
        extflash_info_t i;
        extflash_get_info(&i);
        console_printf("[flash] t=%lu JEDEC %02X %02X %02X size=%lu bytes\r\n", T_NOW(), i.jedec[0], i.jedec[1],
                       i.jedec[2], (unsigned long)i.size_bytes);
    }
    return 0;
}

static int cmd_flash_test(int argc, char **argv)
{
    (void)argv;
    if (argc != 0) {
        return 1;
    }
    if (!flash_ready()) {
        return 0;
    }
    extflash_test_t r;
    const TickType_t t0 = xTaskGetTickCount();
    status_t st = extflash_selftest(&r);
    console_printf("[flash] t=%lu test %s in %lu ms: sector 0x%06lX was_blank=%s erase=%lu ms "
                   "program 4096 B=%lu us\r\n",
                   T_NOW(), status_str(st), (unsigned long)(xTaskGetTickCount() - t0), (unsigned long)r.addr,
                   r.was_blank ? "yes" : "no (saved, restored)", (unsigned long)r.erase_ms,
                   (unsigned long)r.program_us);
    console_printf("[flash] errors: erase=%u verify=%u restore=%u (bytes)\r\n", r.erase_errors, r.verify_errors,
                   r.restore_errors);
    return 0;
}

static const cli_cmd_t s_cmds[] = {
    {"sd", "", "microSD: detect, init, card info, read sector 0 (no writes)", cmd_sd},
    {"flash", "", "SPI flash JEDEC ID and SFDP capacity (inits on first use)", cmd_flash},
    {"flash test", "", "erase/program/verify the last 4 KB sector, then restore it", cmd_flash_test},
};

void storage_cmds_register(void)
{
    cli_register(s_cmds, sizeof s_cmds / sizeof s_cmds[0]);
}
