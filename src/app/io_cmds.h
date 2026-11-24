/*
 * io_cmds.h: DIAG console commands for the indicator LED, the buzzer, the
 * arrow keys, the LED matrix and IR (M2, PRD 5.1 DIAG self-test and 8.6 console commands).
 */
#ifndef IO_CMDS_H
#define IO_CMDS_H

/* Register the commands with the CLI. Call before the scheduler starts. */
void io_cmds_register(void);

#endif /* IO_CMDS_H */
