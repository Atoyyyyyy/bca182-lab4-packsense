/*
 * rtc_cmds.h: console commands for the RTC (M2 bring-up, PRD 8.6).
 */
#ifndef RTC_CMDS_H
#define RTC_CMDS_H

/* Register the commands with the CLI. Call before the scheduler starts. */
void rtc_cmds_register(void);

#endif /* RTC_CMDS_H */
