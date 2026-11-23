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
 * | SysTick/PendSV| 15       | kernel          |
 */
#include "stm32f4xx_hal.h"

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
