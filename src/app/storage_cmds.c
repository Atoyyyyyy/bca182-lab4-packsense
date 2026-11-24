/*
 * storage_cmds.c: console commands for the storage devices.
 *
 *   sd      card-detect pin, SDIO init, card info, read-only sector-0 check
 *
 * Nothing here writes to the card.
 */
#include "storage_cmds.h"

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include "app/cli.h"
#include "drivers/console_uart.h"
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

static const cli_cmd_t s_cmds[] = {
    {"sd", "", "microSD: detect, init, card info, read sector 0 (no writes)", cmd_sd},
};

void storage_cmds_register(void)
{
    cli_register(s_cmds, sizeof s_cmds / sizeof s_cmds[0]);
}
