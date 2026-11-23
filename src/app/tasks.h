/*
 * tasks.h: creates the application's FreeRTOS tasks.
 * M1: heartbeat only. The PRD 6.3 task table is filled in from M2 on.
 */
#ifndef TASKS_H
#define TASKS_H

/* Create every task. Call once from main() before vTaskStartScheduler(). */
void tasks_create(void);

#endif /* TASKS_H */
