/*
 * storage_cmds.h: console commands for the microSD card and the SPI flash
 * (M2 bring-up, PRD 5.1 DIAG "SD/flash read-write test").
 */
#ifndef STORAGE_CMDS_H
#define STORAGE_CMDS_H

/* Register the commands with the CLI. Call before the scheduler starts. */
void storage_cmds_register(void);

#endif /* STORAGE_CMDS_H */
