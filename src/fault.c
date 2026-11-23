#include "fault.h"

#include <stdio.h>

#include "stm32f4xx_hal.h"

#include "board_pins.h"
#include "drivers/console_uart.h"

static void halt_blinking(void) __attribute__((noreturn));

static void halt_blinking(void)
{
    /* Stop in the debugger if one is attached. */
    if ((CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) != 0U) {
        __BKPT(0);
    }
    /* Fast red blink (~5 Hz at 168 MHz) without timers or interrupts. */
    for (;;) {
        LED_RED_PORT->ODR ^= LED_RED_PIN;
        for (volatile uint32_t i = 0; i < 3000000U; i++) {
        }
    }
}

void app_panic(const char *what, const char *detail)
{
    __disable_irq();
    console_panic_write("\r\n!!! PANIC: ");
    console_panic_write(what);
    if (detail != NULL) {
        console_panic_write(": ");
        console_panic_write(detail);
    }
    console_panic_write("\r\n");
    halt_blinking();
}

void fault_report(const char *name, const uint32_t *frame)
{
    char buf[96];

    __disable_irq();
    console_panic_write("\r\n!!! FAULT: ");
    console_panic_write(name);
    console_panic_write("\r\n");
    /* Stacked frame: r0 r1 r2 r3 r12 lr pc xpsr. snprintf is safe here: it
       does not allocate for integer formats. */
    (void)snprintf(buf, sizeof buf, "  pc=0x%08lx lr=0x%08lx xpsr=0x%08lx\r\n",
                   (unsigned long)frame[6], (unsigned long)frame[5], (unsigned long)frame[7]);
    console_panic_write(buf);
    (void)snprintf(buf, sizeof buf, "  cfsr=0x%08lx hfsr=0x%08lx mmfar=0x%08lx bfar=0x%08lx\r\n",
                   (unsigned long)SCB->CFSR, (unsigned long)SCB->HFSR,
                   (unsigned long)SCB->MMFAR, (unsigned long)SCB->BFAR);
    console_panic_write(buf);
    console_panic_write("  (find pc with: arm-none-eabi-addr2line -e .pio/build/rt_spark/firmware.elf <pc>)\r\n");
    halt_blinking();
}

/* HAL parameter check failure (USE_FULL_ASSERT). */
void assert_failed(uint8_t *file, uint32_t line)
{
    char buf[24];
    (void)snprintf(buf, sizeof buf, ":%lu", (unsigned long)line);
    __disable_irq();
    console_panic_write("\r\n!!! HAL assert: ");
    console_panic_write((const char *)file);
    console_panic_write(buf);
    console_panic_write("\r\n");
    halt_blinking();
}

/* configASSERT() failure. */
void app_assert_failed(const char *file, int line)
{
    char buf[24];
    (void)snprintf(buf, sizeof buf, ":%d", line);
    __disable_irq();
    console_panic_write("\r\n!!! RTOS assert: ");
    console_panic_write(file);
    console_panic_write(buf);
    console_panic_write("\r\n");
    halt_blinking();
}
