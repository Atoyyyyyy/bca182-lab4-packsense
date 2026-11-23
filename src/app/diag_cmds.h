/*
 * diag_cmds.h: DIAG console commands for buses and sensors (M2, PRD 5.1 DIAG
 * mode and 8.6 console commands).
 */
#ifndef DIAG_CMDS_H
#define DIAG_CMDS_H

/* Register the commands with the CLI. Call before the scheduler starts. */
void diag_cmds_register(void);

#endif /* DIAG_CMDS_H */
