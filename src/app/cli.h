/*
 * cli.h: console command line (PRD 8.6) over the ST-LINK virtual COM port.
 *
 * Modules register tables of commands. A command name may have several words
 * ("diag mem"); the longest name matching the first words of the typed line
 * wins, and the remaining words are passed as argv (argv[0] = first argument).
 */
#ifndef CLI_H
#define CLI_H

#include <stdbool.h>
#include <stddef.h>

typedef int (*cli_fn_t)(int argc, char **argv); /* return 0 on success */

typedef struct {
    const char *name;  /* "diag mem" */
    const char *usage; /* argument summary, "" if none */
    const char *help;  /* one line */
    cli_fn_t fn;
} cli_cmd_t;

/* Register `n` commands (the table must stay valid). Call before the
   scheduler starts. */
void cli_register(const cli_cmd_t *cmds, size_t n);

/* Create the CLI task (registers the built-in commands). */
void cli_task_create(void);

/* For long-running commands (streams): true if a key was pressed since the
   command started. Consumes the pending input. CLI task only. */
bool cli_abort_requested(void);

/* Parse a decimal argument. Returns false (and prints an error) if invalid
   or outside [min, max]. */
bool cli_arg_u32(const char *s, unsigned long min, unsigned long max, unsigned long *out);

/* Heartbeat console lines on/off (M1 [hb] lines; the LED always blinks). */
extern volatile bool g_hb_print;

#endif /* CLI_H */
