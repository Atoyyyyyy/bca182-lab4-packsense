/*
 * wifi_cmds.h: console commands for the RW007 Wi-Fi module (M2 bring-up:
 * presence, firmware version, MAC, scan; no join).
 */
#ifndef WIFI_CMDS_H
#define WIFI_CMDS_H

/* Register the commands with the CLI. Call before the scheduler starts. */
void wifi_cmds_register(void);

#endif /* WIFI_CMDS_H */
