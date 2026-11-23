/*
 * tasks.h: creates the application's FreeRTOS tasks.
 * M1: heartbeat only. The PRD 6.3 task table is filled in from M2 on.
 */
#ifndef TASKS_H
#define TASKS_H

#include <stdbool.h>

/* Create every task. Call once from main() before vTaskStartScheduler(). */
void tasks_create(void);

/* true (default): the heartbeat blinks the red LED at 1 Hz. The `led`
   console command clears it to drive the LED by hand. */
extern volatile bool g_hb_led;

#endif /* TASKS_H */
