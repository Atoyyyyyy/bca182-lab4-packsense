/*
 * tasks.c: application tasks.
 *
 * HeartbeatTask (M1 bring-up): toggles the red LED every HEARTBEAT_PERIOD_MS
 * with vTaskDelayUntil (no drift), so the LED blinks at 1 Hz, and prints one
 * status line per second. tools/check_heartbeat.py compares the tick count in
 * these lines with the PC's timestamps to check the real clock rate.
 *
 * Its stack and TCB are static and live in CCM RAM (CPU-only memory), which
 * also proves the .ccmram section works.
 */
#include "tasks.h"

#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include "app/cli.h"
#include "app_config.h"
#include "drivers/console_uart.h"
#include "drivers/led.h"

volatile bool g_hb_led = true;

static CCM_BSS StackType_t s_hb_stack[HEARTBEAT_TASK_STACK_WORDS];
static CCM_BSS StaticTask_t s_hb_tcb;

static void heartbeat_task(void *arg)
{
    (void)arg;
    const TickType_t period = pdMS_TO_TICKS(HEARTBEAT_PERIOD_MS);
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t toggles = 0U;
    uint32_t line_no = 0U;

    console_printf("[rtos] scheduler started, first task running at tick %lu\r\n",
                   (unsigned long)last_wake);

    for (;;) {
        if (g_hb_led) {
            led_toggle(LED_RED);
        }
        toggles++;

        if ((toggles % HEARTBEAT_REPORT_EVERY) == 0U) {
            line_no++;
        }
        if ((toggles % HEARTBEAT_REPORT_EVERY) == 0U && g_hb_print) {
            console_printf("[hb] n=%lu tick=%lu led=%s heap_free=%u heap_min=%u hb_hwm=%lu\r\n",
                           (unsigned long)line_no, (unsigned long)xTaskGetTickCount(),
                           led_is_on(LED_RED) ? "on" : "off",
                           (unsigned)xPortGetFreeHeapSize(),
                           (unsigned)xPortGetMinimumEverFreeHeapSize(),
                           (unsigned long)uxTaskGetStackHighWaterMark(NULL));
        }

        (void)xTaskDelayUntil(&last_wake, period);
    }
}

void tasks_create(void)
{
    TaskHandle_t h = xTaskCreateStatic(heartbeat_task, "Heartbeat", HEARTBEAT_TASK_STACK_WORDS,
                                       NULL, HEARTBEAT_TASK_PRIO, s_hb_stack, &s_hb_tcb);
    configASSERT(h != NULL);

    cli_task_create();
}
