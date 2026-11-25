/*
 * wifi_cmds.c: `wifi`, `wifi scan`, `wifi stats`.
 *
 * Scan lines show RSSI, channel, security, the BSSID vendor prefix (OUI)
 * and only the length of each network name, so logs committed to the repo
 * do not publish nearby networks' names or addresses.
 */
#include "wifi_cmds.h"

#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include "app/cli.h"
#include "drivers/console_uart.h"
#include "drivers/rw007.h"

#define T_NOW() ((unsigned long)xTaskGetTickCount())

/* rt_wlan_security_t values (RT-Thread wlan_dev.h, as used by the RW007). */
static const char *sec_name(uint32_t s)
{
    switch (s) {
    case 0x00000000UL: return "open";
    case 0x00000001UL: return "WEP";
    case 0x00008001UL: return "WEP-shared";
    case 0x00200002UL: return "WPA-TKIP";
    case 0x00200004UL: return "WPA-AES";
    case 0x00400002UL: return "WPA2-TKIP";
    case 0x00400004UL: return "WPA2-AES";
    case 0x00400006UL: return "WPA2-mixed";
    case 0x10000000UL: return "WPS-open";
    case 0x10000004UL: return "WPS-AES";
    default: return "other";
    }
}

static void print_stats(void)
{
    rw007_stats_t s;
    rw007_get_stats(&s);
    console_printf("[wifi] stats: transfers=%lu retries=%lu stage1_err=%lu stage2_err=%lu int_timeouts=%lu "
                   "int_edges=%lu rx=%lu unsolicited=%lu\r\n",
                   (unsigned long)s.transfers, (unsigned long)s.retries, (unsigned long)s.stage1_err,
                   (unsigned long)s.stage2_err, (unsigned long)s.int_timeouts, (unsigned long)s.int_edges,
                   (unsigned long)s.rx_packets, (unsigned long)s.unsolicited);
}

static int cmd_wifi(int argc, char **argv)
{
    (void)argv;
    if (argc != 0) {
        return 1;
    }
    rw007_info_t i;
    console_printf("[wifi] t=%lu reset RW007 (RST PG15), waiting for INT PG11\r\n", T_NOW());
    status_t st = rw007_init(&i);
    if (st == ST_NOT_PRESENT) {
        console_printf("[wifi] t=%lu NOT_PRESENT: INT stayed low for 3 s after reset (module not fitted?)\r\n",
                       T_NOW());
        return 0;
    }
    console_printf("[wifi] t=%lu boot: INT high %lu ms after reset; wake window: %s\r\n", T_NOW(),
                   (unsigned long)i.boot_ms, i.wake_no_answer ? "no answer (expected)" : "answered");
    if (st != ST_OK) {
        console_printf("[wifi] t=%lu init %s after %lu ms\r\n", T_NOW(), status_str(st), (unsigned long)i.init_ms);
        print_stats();
        return 0;
    }
    console_printf("[wifi] t=%lu init OK in %lu ms: station mode, sn=[%s] fw=[%s]\r\n", T_NOW(),
                   (unsigned long)i.init_ms, i.sn, i.version);
    console_printf("[wifi] mac %02X:%02X:%02X:%02X:%02X:%02X\r\n", i.mac[0], i.mac[1], i.mac[2], i.mac[3], i.mac[4],
                   i.mac[5]);
    print_stats();
    return 0;
}

static int cmd_wifi_scan(int argc, char **argv)
{
    (void)argv;
    if (argc != 0) {
        return 1;
    }
    if (!rw007_is_ready()) {
        console_printf("[wifi] not initialised: run 'wifi' first\r\n");
        return 0;
    }
    static rw007_scan_t sc;
    status_t st = rw007_scan(15000U, &sc);
    console_printf("[wifi] t=%lu scan %s in %lu ms: %u networks%s%s\r\n", T_NOW(), status_str(st),
                   (unsigned long)sc.ms, sc.count, sc.done ? " (SCAN_DONE)" : " (no SCAN_DONE)",
                   sc.count > RW007_SCAN_MAX ? ", list truncated" : "");
    int best = -1000, worst = 1000;
    for (unsigned k = 0; k < sc.count && k < RW007_SCAN_MAX; k++) {
        const rw007_ap_t *a = &sc.ap[k];
        console_printf("[wifi]   %2u rssi=%4d dBm ch=%2d sec=%-10s oui=%02X:%02X:%02X name_len=%u%s\r\n", k, a->rssi,
                       a->channel, sec_name(a->security), a->bssid[0], a->bssid[1], a->bssid[2], a->ssid_len,
                       a->ssid_len ? "" : " (hidden)");
        if (a->rssi > best) {
            best = a->rssi;
        }
        if (a->rssi < worst) {
            worst = a->rssi;
        }
    }
    if (sc.count) {
        console_printf("[wifi] rssi range %d .. %d dBm\r\n", worst, best);
    }
    print_stats();
    return 0;
}

static int cmd_wifi_stats(int argc, char **argv)
{
    (void)argv;
    if (argc != 0) {
        return 1;
    }
    print_stats();
    return 0;
}

static int cmd_wifi_off(int argc, char **argv)
{
    (void)argv;
    if (argc != 0) {
        return 1;
    }
    rw007_off();
    console_puts("[wifi] RW007 held in reset (radio off); `wifi on` starts it again\r\n");
    return 0;
}

static const cli_cmd_t s_cmds[] = {
    {"wifi", "", "reset + init the RW007 (sn, firmware, MAC)", cmd_wifi},
    {"wifi on", "", "same as `wifi`", cmd_wifi},
    {"wifi off", "", "hold the RW007 in reset (offline test, AC-7)", cmd_wifi_off},
    {"wifi scan", "", "scan for networks (names masked in the output)", cmd_wifi_scan},
    {"wifi stats", "", "RW007 SPI transfer counters", cmd_wifi_stats},
};

void wifi_cmds_register(void)
{
    cli_register(s_cmds, sizeof s_cmds / sizeof s_cmds[0]);
}
