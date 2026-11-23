/*
 * freertos_hooks.c: application hooks required by FreeRTOSConfig.h, and the
 * run-time statistics counter.
 *
 * Run-time stats clock: DWT CYCCNT (CPU cycles, 168 MHz). CYCCNT is 32 bits
 * and wraps every ~25.6 s, so it is extended to 64 bits in software. The tick
 * hook reads it every 1 ms, so no wrap is ever missed. The kernel also reads it
 * from task context (uxTaskGetSystemState runs with only the scheduler
 * suspended, while the tick hook can still fire), so the read-and-extend step
 * masks kernel-level interrupts. For short timing measurements use
 * DWT->CYCCNT directly.
 */
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include "stm32f4xx.h"

#include "fault.h"

static uint32_t s_cyc_last;
static uint32_t s_cyc_high;

void rtstats_timer_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    s_cyc_last = 0U;
    s_cyc_high = 0U;
}

uint64_t rtstats_counter(void)
{
    /* Safe from tasks and from ISRs at priority >= 5 (saves/restores BASEPRI). */
    UBaseType_t saved = portSET_INTERRUPT_MASK_FROM_ISR();
    uint32_t now = DWT->CYCCNT;
    if (now < s_cyc_last) {
        s_cyc_high++;
    }
    s_cyc_last = now;
    uint64_t value = ((uint64_t)s_cyc_high << 32) | now;
    portCLEAR_INTERRUPT_MASK_FROM_ISR(saved);
    return value;
}

void vApplicationTickHook(void)
{
    (void)rtstats_counter(); /* never let CYCCNT wrap unseen */
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    app_panic("stack overflow in task", name);
}

void vApplicationMallocFailedHook(void)
{
    app_panic("FreeRTOS heap exhausted (pvPortMalloc returned NULL)", NULL);
}
