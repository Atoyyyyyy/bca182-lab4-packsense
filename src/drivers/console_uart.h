/*
 * console_uart.h: debug console on USART1 (ST-LINK virtual COM port).
 *
 * Before the scheduler runs, output is polled (blocking). Once the scheduler
 * runs, output goes through a TX ring buffer drained by the USART1 interrupt,
 * and a mutex keeps lines from different tasks from interleaving.
 * Never call console_printf()/console_write() from an ISR; use
 * console_panic_write() only from fault handlers.
 */
#ifndef CONSOLE_UART_H
#define CONSOLE_UART_H

#include <stddef.h>
#include <stdint.h>

/* GPIO + USART1 init (115200 8N1). Call after the system clock is set. */
void console_init(void);

/* Create the mutex. Call once before vTaskStartScheduler(). */
void console_rtos_init(void);

void console_write(const char *data, size_t len);
void console_puts(const char *s);
void console_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Polled, lock-free output for fault/assert handlers (interrupts disabled). */
void console_panic_write(const char *s);

/* Received bytes (filled by the USART1 RX interrupt). Read by one task only
   (the CLI). Blocks up to `wait` ticks for the first byte; returns the number
   of bytes copied (0 on timeout). */
size_t console_read(char *buf, size_t max, uint32_t wait);

/* Bytes dropped because the RX buffer was full or the UART overran. */
uint32_t console_rx_dropped(void);

/* USART1 IRQ body, called from USART1_IRQHandler. */
void console_uart_irq(void);

#endif /* CONSOLE_UART_H */
