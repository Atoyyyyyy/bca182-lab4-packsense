/*
 * diag_cmds.c: console readouts for the I2C buses and the sensors.
 *
 * Every line starts with a tag in brackets so logs can be grepped, and carries
 * t=<ms since boot> so tools can measure timing.
 */
#include "diag_cmds.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include "app/cli.h"
#include "app/fmt.h"
#include "board_pins.h"
#include "drivers/console_uart.h"
#include "drivers/i2c_bus.h"
#include "drivers/icm20608.h"

#define T_NOW() ((unsigned long)xTaskGetTickCount())

/* Parse optional "<n> [period_ms]" arguments of the stream commands. */
static bool stream_args(int argc, char **argv, unsigned long def_n, unsigned long min_period,
                        unsigned long def_period, unsigned long *n, unsigned long *period)
{
    *n = def_n;
    *period = def_period;
    if (argc > 2) {
        return false;
    }
    if (argc >= 1 && !cli_arg_u32(argv[0], 1UL, 100000UL, n)) {
        return false;
    }
    if (argc == 2 && !cli_arg_u32(argv[1], min_period, 60000UL, period)) {
        return false;
    }
    return true;
}

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

/* ---- imu (ICM-20608-G) ---------------------------------------------------- */

static bool imu_ready(void)
{
    icm_info_t info;
    icm20608_get_info(&info);
    if (info.initialised) {
        return true;
    }
    status_t st = icm20608_init();
    icm20608_get_info(&info);
    console_printf("[imu] t=%lu init %s who_am_i=0x%02X (expect 0xAF)\r\n", T_NOW(), status_str(st),
                   info.who_am_i);
    return st == ST_OK;
}

static int cmd_imu(int argc, char **argv)
{
    (void)argv;
    if (argc != 0) {
        return 1;
    }
    if (!imu_ready()) {
        return 0;
    }
    icm_sample_t s;
    icm_info_t info;
    status_t st = icm20608_read(&s);
    icm20608_get_info(&info);
    if (st != ST_OK && st != ST_RANGE_ERR) {
        console_printf("[imu] t=%lu read %s reads=%lu errors=%lu\r\n", T_NOW(), status_str(st),
                       (unsigned long)info.reads, (unsigned long)info.errors);
        return 0;
    }
    char b[8][FMT_F_BUF];
    const float norm = sqrtf(s.ax * s.ax + s.ay * s.ay + s.az * s.az);
    console_printf("[imu] t=%lu a=(%s, %s, %s) m/s2 |a|=%s g=(%s, %s, %s) dps temp=%s C st=%s\r\n",
                   (unsigned long)s.t_ms, fmt_f(b[0], FMT_F_BUF, s.ax, 3), fmt_f(b[1], FMT_F_BUF, s.ay, 3),
                   fmt_f(b[2], FMT_F_BUF, s.az, 3), fmt_f(b[3], FMT_F_BUF, norm, 3),
                   fmt_f(b[4], FMT_F_BUF, s.gx, 2), fmt_f(b[5], FMT_F_BUF, s.gy, 2),
                   fmt_f(b[6], FMT_F_BUF, s.gz, 2), fmt_f(b[7], FMT_F_BUF, s.temp_c, 2), status_str(st));
    console_printf("[imu] who_am_i=0x%02X reads=%lu errors=%lu gyro_bias=(%s, %s, %s) dps\r\n",
                   info.who_am_i, (unsigned long)info.reads, (unsigned long)info.errors,
                   fmt_f(b[0], FMT_F_BUF, info.bias_gx, 3), fmt_f(b[1], FMT_F_BUF, info.bias_gy, 3),
                   fmt_f(b[2], FMT_F_BUF, info.bias_gz, 3));
    return 0;
}

static int cmd_imu_stream(int argc, char **argv)
{
    unsigned long n, period;
    if (!stream_args(argc, argv, 500UL, 10UL, 10UL, &n, &period)) {
        return 1;
    }
    if (!imu_ready()) {
        return 0;
    }
    console_printf("# imu stream n=%lu period_ms=%lu cols=t_ms,ax,ay,az,gx,gy,gz,temp_c,status "
                   "(m/s2, dps, C)\r\n", n, period);
    TickType_t wake = xTaskGetTickCount();
    unsigned long done = 0U, errors = 0U;
    for (; done < n; done++) {
        if (cli_abort_requested()) {
            break;
        }
        icm_sample_t s;
        status_t st = icm20608_read(&s);
        char b[7][FMT_F_BUF];
        if (st == ST_OK || st == ST_RANGE_ERR) {
            console_printf("imu,%lu,%s,%s,%s,%s,%s,%s,%s,%s\r\n", (unsigned long)s.t_ms,
                           fmt_f(b[0], FMT_F_BUF, s.ax, 3), fmt_f(b[1], FMT_F_BUF, s.ay, 3),
                           fmt_f(b[2], FMT_F_BUF, s.az, 3), fmt_f(b[3], FMT_F_BUF, s.gx, 2),
                           fmt_f(b[4], FMT_F_BUF, s.gy, 2), fmt_f(b[5], FMT_F_BUF, s.gz, 2),
                           fmt_f(b[6], FMT_F_BUF, s.temp_c, 2), status_str(st));
        } else {
            errors++;
            console_printf("imu,%lu,,,,,,,,%s\r\n", T_NOW(), status_str(st));
        }
        (void)xTaskDelayUntil(&wake, pdMS_TO_TICKS(period));
    }
    console_printf("# imu stream end: %lu samples, %lu errors\r\n", done, errors);
    return 0;
}

static int cmd_imu_cal(int argc, char **argv)
{
    unsigned long n = 200UL;
    if (argc > 1 || (argc == 1 && !cli_arg_u32(argv[0], 10UL, 3000UL, &n))) {
        return 1;
    }
    if (!imu_ready()) {
        return 0;
    }
    console_printf("[imu] t=%lu calibrating gyro over %lu samples (keep the board still)...\r\n", T_NOW(), n);
    float norm = 0.0f;
    status_t st = icm20608_calibrate_gyro(n, &norm);
    icm_info_t info;
    icm20608_get_info(&info);
    char b[4][FMT_F_BUF];
    console_printf("[imu] t=%lu cal %s gyro_bias=(%s, %s, %s) dps mean|a|=%s m/s2 (expect ~9.81)\r\n",
                   T_NOW(), status_str(st), fmt_f(b[0], FMT_F_BUF, info.bias_gx, 3),
                   fmt_f(b[1], FMT_F_BUF, info.bias_gy, 3), fmt_f(b[2], FMT_F_BUF, info.bias_gz, 3),
                   fmt_f(b[3], FMT_F_BUF, norm, 3));
    return 0;
}

static const cli_cmd_t s_cmds[] = {
    {"i2c scan", "", "list devices that answer on both I2C buses", cmd_i2c_scan},
    {"i2c stats", "", "transfer and error counters per bus", cmd_i2c_stats},
    {"imu", "", "one IMU reading (inits the IMU on first use)", cmd_imu},
    {"imu stream", "[n] [period_ms]", "n IMU samples as CSV (default 500 @ 10 ms)", cmd_imu_stream},
    {"imu cal", "[samples]", "gyro bias calibration, board still (default 200)", cmd_imu_cal},
};

void diag_cmds_register(void)
{
    cli_register(s_cmds, sizeof s_cmds / sizeof s_cmds[0]);
}
