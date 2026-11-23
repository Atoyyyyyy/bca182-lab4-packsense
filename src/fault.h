/*
 * fault.h: last-resort error reporting (asserts, faults, RTOS hooks).
 * Prints over the console with polling, then halts with the red LED blinking
 * fast. (From M5 the IWDG will reset the board instead of halting forever.)
 */
#ifndef FAULT_H
#define FAULT_H

#include <stdint.h>

void app_panic(const char *what, const char *detail) __attribute__((noreturn));

/* Called by the HardFault/MemManage/BusFault/UsageFault handlers with the
   exception stack frame. */
void fault_report(const char *name, const uint32_t *frame) __attribute__((noreturn));

#endif /* FAULT_H */
