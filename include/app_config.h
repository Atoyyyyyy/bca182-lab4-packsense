/*
 * app_config.h: firmware-wide settings (MCU-side choices, not board facts).
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#define PACKSENSE_FW_NAME          "PackSense"
#define PACKSENSE_FW_VERSION       "0.5.0-m5"

#ifndef PACKSENSE_GIT_REV
#define PACKSENSE_GIT_REV          "unknown" /* set by tools/pio_git_rev.py */
#endif

/* Console */
#define CONSOLE_BAUD               115200U
#define CONSOLE_TX_BUF_SIZE        1024U /* bytes, power of two */
#define CONSOLE_LINE_MAX           256U  /* longest formatted line ([win] is ~210) */
#define CONSOLE_RX_BUF_SIZE        128U  /* bytes typed ahead of the CLI */

/* NVIC priorities (0 = highest). See the rules in FreeRTOSConfig.h. */
#define IRQ_PRIO_I2C_MAIN          5U
#define IRQ_PRIO_CONSOLE_USART     6U
#define IRQ_PRIO_BUTTONS           7U  /* EXTI0/1/4/9_5: xQueueSendFromISR */
#define IRQ_PRIO_WIFI_INT          7U  /* EXTI15_10 (RW007 INT): xSemaphoreGiveFromISR */

/* Task-notification slot used by drivers to wait for I/O completion
   (slot 0 stays free for application events). */
#define NOTIFY_IDX_IO              1U

/* Tasks (priorities from PRD 6.3; heartbeat is a bring-up task) */
#define HEARTBEAT_TASK_PRIO        1U
#define HEARTBEAT_TASK_STACK_WORDS 256U
#define HEARTBEAT_PERIOD_MS        500U  /* LED toggles every 500 ms -> 1 Hz blink */
#define HEARTBEAT_REPORT_EVERY     2U    /* one console line per 2 toggles (1 s) */

/* Console command task (M2 bring-up; PRD 8.6 puts the parser in Ui/LogTask
   later). It also runs the DIAG readouts, so it calls the sensor drivers. */
#define CLI_TASK_PRIO              2U
#define CLI_TASK_STACK_WORDS       1024U
#define CLI_LINE_MAX               96U
#define CLI_MAX_ARGS               8U
#define CLI_MAX_COMMANDS           32U

/* SensorTask / EnvTask (PRD 6.3) */
#define SENSOR_TASK_PRIO           5U
#define SENSOR_TASK_STACK_WORDS    512U
#define SENSOR_PERIOD_MS           10U   /* IMU 100 Hz */
#define SENSOR_ALS_EVERY           5U    /* AP3216C every 5th tick = 20 Hz */
#define SENSOR_QUEUE_LEN           64U   /* 0.64 s of samples */
#define ENV_TASK_PRIO              2U
#define ENV_TASK_STACK_WORDS       384U
#define ENV_PERIOD_MS              1000U

/* Sensor fault handling (PRD 6.4) */
#define SENSOR_FAIL_LIMIT          3U    /* consecutive failed reads -> DEGRADED */
#define SENSOR_REINIT_MS           5000U /* DEGRADED: re-init attempt period */

/* Capture mode (M3): writes samples to the SPI flash log */
#define CAPTURE_TASK_PRIO          3U
#define CAPTURE_TASK_STACK_WORDS   768U

/* Product firmware (M5, PRD 6.3) */
#define INFER_TASK_PRIO            4U
#define INFER_TASK_STACK_WORDS     1024U
#define INFER_RESULT_QUEUE_LEN     4U
#define INFER_MAX_INVALID          10U   /* > 5 % of a 200-sample window -> not classified */
#define DECISION_MIN_CONF          0.5f  /* smoothed confidence below this = uncertain */
#define UI_TASK_PRIO               3U
#define UI_TASK_STACK_WORDS        768U
#define UI_PERIOD_MS               50U
#define UI_DIAG_PERIOD_MS          500U
#define UI_SELFTEST_SHOW_MS        2000U
#define UI_BANNER_MS               4000U /* "Rough handling detected" etc. */
#define LOG_TASK_PRIO              1U
#define LOG_TASK_STACK_WORDS       1024U
#define LOG_QUEUE_LEN              16U

/* Climate rule (AHT, context only; thresholds can be changed with `climate`) */
#define CLIMATE_T_HIGH_C           40.0f
#define CLIMATE_RH_HIGH            80.0f
#define CLIMATE_HOLD_S             30U   /* above a threshold this long -> warning */
#define CLIMATE_HYST_C             2.0f
#define CLIMATE_HYST_RH            5.0f

/* Independent watchdog: fed by the heartbeat task only while every
   monitored task has checked in during the last window. */
#define WDG_TIMEOUT_MS             4000U
#define WDG_WINDOW_MS              2000U

/* I2C */
#define I2C_MAIN_CLOCK_HZ          400000U /* fast mode: ICM-20608, AP3216C, ES8388 */
#define I2C_SOFT_HALF_PERIOD_US    5U      /* ~100 kHz bit-banged bus (AHT) */
#define I2C_TIMEOUT_MS             20U

/* Buttons (PRD 6.2 buttons.c) */
#define BUTTON_DEBOUNCE_MS         30U   /* key must be quiet this long */
#define BUTTON_LONG_PRESS_MS       800U  /* held at least this long = long press */
#define BUTTON_RAW_QUEUE_LEN       32U   /* raw edges from the EXTI ISR */

/* Place a zero-initialised object in CCM RAM (CPU-only, no DMA). The section is
   NOLOAD and zeroed by ccmram_zero() in main(); initialisers are NOT applied. */
#define CCM_BSS                    __attribute__((section(".ccmram")))

#endif /* APP_CONFIG_H */
