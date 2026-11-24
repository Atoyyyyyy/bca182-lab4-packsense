/*
 * selftest.h: `selftest model` console command (host-device parity, AC-3).
 */
#ifndef SELFTEST_H
#define SELFTEST_H

/* Register the commands with the CLI. Call before the scheduler starts. */
void selftest_cmds_register(void);

#endif /* SELFTEST_H */
