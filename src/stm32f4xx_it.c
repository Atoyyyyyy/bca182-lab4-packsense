/*
 * stm32f4xx_it.c: interrupt and exception handlers.
 *
 * SVC_Handler, PendSV_Handler and SysTick_Handler are deliberately NOT defined
 * here: FreeRTOSConfig.h maps the port's vPortSVCHandler, xPortPendSVHandler
 * and xPortSysTickHandler onto those names. Defining them here as well would
 * give duplicate-symbol errors (or silently break the scheduler).
 *
 * | IRQ           | Priority | Calls FreeRTOS? |
 * |---------------|----------|-----------------|
 * | TIM6_DAC      | 4        | no (HAL tick)   |
 * | I2C2_EV/ER    | 5        | yes (notify)    |
 * | USART1        | 6        | yes (RX stream) |
 * | EXTI0/1/4/9_5 | 7        | yes (key queue); EXTI8 = IR RX, no RTOS call |
 * | EXTI15_10     | 7        | yes (RW007 INT semaphore) |
 * | SysTick/PendSV| 15       | kernel          |
 */
#include "stm32f4xx_hal.h"

#include "drivers/buttons.h"
#include "drivers/ir.h"
#include "drivers/rw007.h"
#include "drivers/console_uart.h"
#include "drivers/i2c_bus.h"
#include "fault.h"
#include "hal_timebase_tim6.h"

/* Pass the active stack pointer (MSP or PSP, from EXC_RETURN bit 2) to C. */
#define FAULT_ENTRY(handler, label)                                            \
    __attribute__((naked)) void handler(void)                                  \
    {                                                                          \
        __asm volatile("tst   lr, #4        \n"                                \
                       "ite   eq            \n"                                \
                       "mrseq r0, msp       \n"                                \
                       "mrsne r0, psp       \n"                                \
                       "b     " #handler "_c\n");                              \
    }                                                                          \
    void handler##_c(const uint32_t *frame) __attribute__((used, noreturn));   \
    void handler##_c(const uint32_t *frame)                                    \
    {                                                                          \
        fault_report(label, frame);                                            \
    }

FAULT_ENTRY(HardFault_Handler, "HardFault")
FAULT_ENTRY(MemManage_Handler, "MemManage")
FAULT_ENTRY(BusFault_Handler, "BusFault")
FAULT_ENTRY(UsageFault_Handler, "UsageFault")

void NMI_Handler(void)
{
    app_panic("NMI", NULL);
}

void DebugMon_Handler(void)
{
}

void TIM6_DAC_IRQHandler(void)
{
    hal_timebase_irq();
}

void USART1_IRQHandler(void)
{
    console_uart_irq();
}

void I2C2_EV_IRQHandler(void)
{
    i2c_main_ev_irq();
}

void I2C2_ER_IRQHandler(void)
{
    i2c_main_er_irq();
}

/* Arrow keys: LEFT PC0, DOWN PC1, RIGHT PC4, UP PC5. EXTI9_5 is shared with
   later users (IR receiver PF8); buttons_exti_irq() only clears its own pins. */
void EXTI0_IRQHandler(void)
{
    buttons_exti_irq();
}

void EXTI1_IRQHandler(void)
{
    buttons_exti_irq();
}

void EXTI4_IRQHandler(void)
{
    buttons_exti_irq();
}

void EXTI9_5_IRQHandler(void)
{
    buttons_exti_irq(); /* key UP, PC5 = EXTI5 */
    ir_exti_irq();      /* IR receiver, PF8 = EXTI8 */
}

void EXTI15_10_IRQHandler(void)
{
    rw007_exti_irq(); /* RW007 INT, PG11 = EXTI11 */
}
