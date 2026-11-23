/*
 * cli.c: line editor, command dispatch and built-in commands.
 *
 * Input: bytes from console_read() (USART1 RX interrupt -> stream buffer).
 * Echoes what is typed, handles backspace, ends a line at CR or LF.
 * Output of every command goes through console_printf (mutex-protected).
 */
#include "cli.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "stm32f4xx_hal.h"

#include "app_config.h"
#include "drivers/console_uart.h"

volatile bool g_hb_print;

typedef struct {
    const cli_cmd_t *cmds;
    size_t n;
} cli_table_t;

static cli_table_t s_tables[CLI_MAX_COMMANDS];
static size_t s_ntables;

static CCM_BSS StackType_t s_cli_stack[CLI_TASK_STACK_WORDS];
static CCM_BSS StaticTask_t s_cli_tcb;

void cli_register(const cli_cmd_t *cmds, size_t n)
{
    configASSERT(s_ntables < CLI_MAX_COMMANDS);
    s_tables[s_ntables].cmds = cmds;
    s_tables[s_ntables].n = n;
    s_ntables++;
}

bool cli_abort_requested(void)
{
    char c[8];
    return console_read(c, sizeof c, 0U) > 0U;
}

bool cli_arg_u32(const char *s, unsigned long min, unsigned long max, unsigned long *out)
{
    char *end = NULL;
    unsigned long v;

    if (s == NULL || *s == '\0') {
        console_puts("error: missing number\r\n");
        return false;
    }
    v = strtoul(s, &end, 10);
    if (*end != '\0' || v < min || v > max) {
        console_printf("error: '%s' is not a number in %lu..%lu\r\n", s, min, max);
        return false;
    }
    *out = v;
    return true;
}

/* Number of leading words of `name` that match argv, or 0 if not all of
   `name` matches. */
static int name_match(const char *name, int argc, char **argv)
{
    int words = 0;
    const char *p = name;

    while (*p != '\0') {
        const char *sp = strchr(p, ' ');
        size_t len = (sp != NULL) ? (size_t)(sp - p) : strlen(p);
        if (words >= argc || strlen(argv[words]) != len || strncmp(argv[words], p, len) != 0) {
            return 0;
        }
        words++;
        p += len;
        while (*p == ' ') {
            p++;
        }
    }
    return words;
}

static void dispatch(int argc, char **argv)
{
    const cli_cmd_t *best = NULL;
    int best_words = 0;

    for (size_t t = 0; t < s_ntables; t++) {
        for (size_t i = 0; i < s_tables[t].n; i++) {
            int w = name_match(s_tables[t].cmds[i].name, argc, argv);
            if (w > best_words) {
                best_words = w;
                best = &s_tables[t].cmds[i];
            }
        }
    }
    if (best == NULL) {
        console_printf("unknown command '%s' (type 'help')\r\n", argv[0]);
        return;
    }
    int rc = best->fn(argc - best_words, argv + best_words);
    if (rc != 0) {
        console_printf("usage: %s %s\r\n", best->name, best->usage);
    }
}

static int split(char *line, char **argv)
{
    int argc = 0;
    char *p = line;

    while (*p != '\0' && argc < (int)CLI_MAX_ARGS) {
        while (*p == ' ' || *p == '\t') {
            *p++ = '\0';
        }
        if (*p == '\0') {
            break;
        }
        argv[argc++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t') {
            p++;
        }
    }
    return argc;
}

/* ---- built-in commands ------------------------------------------------- */

static int cmd_help(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    console_puts("commands (any key stops a stream):\r\n");
    for (size_t t = 0; t < s_ntables; t++) {
        for (size_t i = 0; i < s_tables[t].n; i++) {
            const cli_cmd_t *c = &s_tables[t].cmds[i];
            console_printf("  %-14s %-36s %s\r\n", c->name, c->usage, c->help);
        }
    }
    return 0;
}

static int cmd_version(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    console_printf("%s %s (git %s) built %s %s\r\n", PACKSENSE_FW_NAME, PACKSENSE_FW_VERSION,
                   PACKSENSE_GIT_REV, __DATE__, __TIME__);
    return 0;
}

static int cmd_uptime(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    uint32_t ms = (uint32_t)xTaskGetTickCount();
    console_printf("uptime %lu ms (%lu s)\r\n", (unsigned long)ms, (unsigned long)(ms / 1000U));
    return 0;
}

static const char *task_state(eTaskState s)
{
    switch (s) {
    case eRunning:   return "run";
    case eReady:     return "ready";
    case eBlocked:   return "blocked";
    case eSuspended: return "susp";
    case eDeleted:   return "deleted";
    default:         return "?";
    }
}

static int cmd_diag_mem(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    enum { MAX_TASKS = 12 };
    static TaskStatus_t st[MAX_TASKS]; /* CLI task only */
    configRUN_TIME_COUNTER_TYPE total = 0U;
    UBaseType_t n = uxTaskGetSystemState(st, MAX_TASKS, &total);

    console_printf("[diag mem] t=%lu heap_free=%u heap_min=%u console_rx_dropped=%lu\r\n",
                   (unsigned long)xTaskGetTickCount(), (unsigned)xPortGetFreeHeapSize(),
                   (unsigned)xPortGetMinimumEverFreeHeapSize(),
                   (unsigned long)console_rx_dropped());
    console_puts("[diag mem] task            prio state    hwm_words cpu_permille\r\n");
    for (UBaseType_t i = 0; i < n; i++) {
        /* CPU share since boot, in 1/1000 (64-bit maths; nano printf has no %llu). */
        uint32_t permille = (total > 0U) ? (uint32_t)((st[i].ulRunTimeCounter * 1000U) / total) : 0U;
        console_printf("[diag mem] %-15s %4lu %-8s %9lu %12lu\r\n", st[i].pcTaskName,
                       (unsigned long)st[i].uxCurrentPriority, task_state(st[i].eCurrentState),
                       (unsigned long)st[i].usStackHighWaterMark, (unsigned long)permille);
    }
    return 0;
}

static int cmd_hb(int argc, char **argv)
{
    if (argc != 1) {
        return 1;
    }
    if (strcmp(argv[0], "on") == 0) {
        g_hb_print = true;
    } else if (strcmp(argv[0], "off") == 0) {
        g_hb_print = false;
    } else {
        return 1;
    }
    return 0;
}

static int cmd_reset(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    console_puts("resetting...\r\n");
    vTaskDelay(pdMS_TO_TICKS(50)); /* let the TX ring drain */
    NVIC_SystemReset();
    return 0;
}

static const cli_cmd_t s_builtin[] = {
    {"help", "", "list commands", cmd_help},
    {"version", "", "firmware version and git revision", cmd_version},
    {"uptime", "", "time since boot", cmd_uptime},
    {"diag mem", "", "heap, per-task stack HWM and CPU share", cmd_diag_mem},
    {"hb", "on|off", "heartbeat lines on the console", cmd_hb},
    {"reset", "", "software reset", cmd_reset},
};

/* ---- task --------------------------------------------------------------- */

static void cli_task(void *arg)
{
    (void)arg;
    static char line[CLI_LINE_MAX];
    size_t len = 0U;
    char prev = '\0';

    console_puts("[cli] ready, type 'help'\r\n> ");
    for (;;) {
        char buf[16];
        size_t n = console_read(buf, sizeof buf, portMAX_DELAY);

        for (size_t i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n' && prev == '\r') {
                prev = c;
                continue; /* CRLF: the CR already ended the line */
            }
            prev = c;
            if (c == '\r' || c == '\n') {
                console_puts("\r\n");
                line[len] = '\0';
                char *argv[CLI_MAX_ARGS];
                int argc = split(line, argv);
                if (argc > 0) {
                    dispatch(argc, argv);
                }
                len = 0U;
                console_puts("> ");
            } else if (c == '\b' || c == 0x7F) {
                if (len > 0U) {
                    len--;
                    console_puts("\b \b");
                }
            } else if (c >= 0x20 && c < 0x7F) {
                if (len < sizeof line - 1U) {
                    line[len++] = c;
                    console_write(&c, 1U);
                }
            }
        }
    }
}

void cli_task_create(void)
{
    cli_register(s_builtin, sizeof s_builtin / sizeof s_builtin[0]);
    TaskHandle_t h = xTaskCreateStatic(cli_task, "CLI", CLI_TASK_STACK_WORDS, NULL, CLI_TASK_PRIO,
                                       s_cli_stack, &s_cli_tcb);
    configASSERT(h != NULL);
}
