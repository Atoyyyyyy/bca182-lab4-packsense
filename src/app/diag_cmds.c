/*
 * diag_cmds.c: console readouts for the I2C buses and the sensors.
 *
 * Every line starts with a tag in brackets so logs can be grepped, and carries
 * t=<ms since boot> so tools can measure timing.
 */
#include "diag_cmds.h"

#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include "app/cli.h"
#include "board_pins.h"
#include "drivers/console_uart.h"
#include "drivers/i2c_bus.h"

/* ---- i2c ---------------------------------------------------------------- */

static const char *known_device(i2c_bus_id_t bus, uint8_t addr)
{
    if (bus == I2C_BUS_MAIN) {
        switch (addr) {
        case I2C_ADDR_ICM20608: return "ICM-20608-G (IMU)";
        case I2C_ADDR_AP3216C:  return "AP3216C (light/proximity)";
        case I2C_ADDR_ES8388:   return "ES8388 (audio codec)";
        default:                return "unexpected";
        }
    }
    return (addr == I2C_ADDR_AHT) ? "AHT10/AHT21 (temp/humidity)" : "unexpected";
}

static int cmd_i2c_scan(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    for (int b = 0; b < I2C_BUS_COUNT; b++) {
        int found = 0;
        for (uint8_t a = 0x08U; a <= 0x77U; a++) {
            status_t st = i2c_probe((i2c_bus_id_t)b, a);
            if (st == ST_OK) {
                console_printf("[i2c] t=%lu bus=%s addr=0x%02X %s\r\n", (unsigned long)xTaskGetTickCount(),
                               i2c_bus_name((i2c_bus_id_t)b), a, known_device((i2c_bus_id_t)b, a));
                found++;
            } else if (st != ST_NACK) {
                console_printf("[i2c] bus=%s addr=0x%02X error %s, scan stopped\r\n",
                               i2c_bus_name((i2c_bus_id_t)b), a, status_str(st));
                break;
            }
        }
        console_printf("[i2c] bus=%s scan done, %d device(s)\r\n", i2c_bus_name((i2c_bus_id_t)b), found);
    }
    return 0;
}

static int cmd_i2c_stats(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    for (int b = 0; b < I2C_BUS_COUNT; b++) {
        i2c_bus_stats_t s;
        i2c_bus_get_stats((i2c_bus_id_t)b, &s);
        console_printf("[i2c] bus=%s transfers=%lu nacks=%lu timeouts=%lu bus_errors=%lu recoveries=%lu\r\n",
                       i2c_bus_name((i2c_bus_id_t)b), (unsigned long)s.transfers, (unsigned long)s.nacks,
                       (unsigned long)s.timeouts, (unsigned long)s.bus_errors, (unsigned long)s.recoveries);
    }
    return 0;
}

static const cli_cmd_t s_cmds[] = {
    {"i2c scan", "", "list devices that answer on both I2C buses", cmd_i2c_scan},
    {"i2c stats", "", "transfer and error counters per bus", cmd_i2c_stats},
};

void diag_cmds_register(void)
{
    cli_register(s_cmds, sizeof s_cmds / sizeof s_cmds[0]);
}
