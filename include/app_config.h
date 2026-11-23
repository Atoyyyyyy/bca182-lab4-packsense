/*
 * app_config.h: firmware-wide settings (MCU-side choices, not board facts).
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#define PACKSENSE_FW_NAME          "PackSense"
#define PACKSENSE_FW_VERSION       "0.1.0-m1"

#ifndef PACKSENSE_GIT_REV
#define PACKSENSE_GIT_REV          "unknown" /* set by tools/pio_git_rev.py */
#endif

/* Console */
#define CONSOLE_BAUD               115200U
#define CONSOLE_TX_BUF_SIZE        1024U /* bytes, power of two */
#define CONSOLE_LINE_MAX           160U  /* longest formatted line */

/* NVIC priorities (0 = highest). See the rules in FreeRTOSConfig.h. */
#define IRQ_PRIO_CONSOLE_USART     6U

/* Tasks (priorities from PRD 6.3; heartbeat is a bring-up task) */
#define HEARTBEAT_TASK_PRIO        1U
#define HEARTBEAT_TASK_STACK_WORDS 256U
#define HEARTBEAT_PERIOD_MS        500U  /* LED toggles every 500 ms -> 1 Hz blink */
#define HEARTBEAT_REPORT_EVERY     2U    /* one console line per 2 toggles (1 s) */

/* Place a zero-initialised object in CCM RAM (CPU-only, no DMA). The section is
   NOLOAD and zeroed by ccmram_zero() in main(); initialisers are NOT applied. */
#define CCM_BSS                    __attribute__((section(".ccmram")))

#endif /* APP_CONFIG_H */
