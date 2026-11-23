/*
 * console_uart.c: USART1 debug console (PA9 TX / PA10 RX -> ST-LINK VCP).
 * Pins: include/board_pins.h (pinmap.md "Debug / console").
 */
#include "console_uart.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "app_config.h"
#include "board_pins.h"

#if (CONSOLE_TX_BUF_SIZE & (CONSOLE_TX_BUF_SIZE - 1U)) != 0U
#error "CONSOLE_TX_BUF_SIZE must be a power of two"
#endif

static UART_HandleTypeDef s_huart;
static SemaphoreHandle_t s_mutex;
static StaticSemaphore_t s_mutex_buf;

/* TX ring: head written by tasks (under s_mutex), tail written by the ISR. */
static uint8_t s_tx_buf[CONSOLE_TX_BUF_SIZE];
static volatile uint32_t s_tx_head;
static volatile uint32_t s_tx_tail;

static char s_line[CONSOLE_LINE_MAX]; /* formatting buffer, guarded by s_mutex */

void console_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();

    gpio.Pin = CONSOLE_TX_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = CONSOLE_GPIO_AF;
    HAL_GPIO_Init(CONSOLE_TX_PORT, &gpio);

    gpio.Pin = CONSOLE_RX_PIN;
    HAL_GPIO_Init(CONSOLE_RX_PORT, &gpio);

    s_huart.Instance = CONSOLE_USART;
    s_huart.Init.BaudRate = CONSOLE_BAUD;
    s_huart.Init.WordLength = UART_WORDLENGTH_8B;
    s_huart.Init.StopBits = UART_STOPBITS_1;
    s_huart.Init.Parity = UART_PARITY_NONE;
    s_huart.Init.Mode = UART_MODE_TX_RX;
    s_huart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    s_huart.Init.OverSampling = UART_OVERSAMPLING_16;
    (void)HAL_UART_Init(&s_huart);

    s_tx_head = 0U;
    s_tx_tail = 0U;

    /* Priority >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY so a later RX
       path may use ...FromISR() APIs. The IRQ stays masked until the scheduler
       starts; output is polled until then. */
    HAL_NVIC_SetPriority(CONSOLE_USART_IRQn, IRQ_PRIO_CONSOLE_USART, 0U);
    HAL_NVIC_EnableIRQ(CONSOLE_USART_IRQn);
}

void console_rtos_init(void)
{
    s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_buf);
    configASSERT(s_mutex != NULL);
}

static void polled_write(const char *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        while ((CONSOLE_USART->SR & USART_SR_TXE) == 0U) {
        }
        CONSOLE_USART->DR = (uint8_t)data[i];
    }
}

static int scheduler_running(void)
{
    return xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
}

/* Caller holds s_mutex. Blocks (vTaskDelay) while the ring is full. */
static void ring_write(const char *data, size_t len)
{
    while (len > 0U) {
        uint32_t head = s_tx_head;
        uint32_t used = head - s_tx_tail;
        uint32_t space = CONSOLE_TX_BUF_SIZE - used;

        if (space == 0U) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        uint32_t n = (len < space) ? (uint32_t)len : space;
        for (uint32_t i = 0; i < n; i++) {
            s_tx_buf[(head + i) & (CONSOLE_TX_BUF_SIZE - 1U)] = (uint8_t)data[i];
        }
        __DMB();
        s_tx_head = head + n;
        CONSOLE_USART->CR1 |= USART_CR1_TXEIE; /* ISR drains the ring */
        data += n;
        len -= n;
    }
}

void console_write(const char *data, size_t len)
{
    if (!scheduler_running()) {
        polled_write(data, len);
        return;
    }
    (void)xSemaphoreTake(s_mutex, portMAX_DELAY);
    ring_write(data, len);
    (void)xSemaphoreGive(s_mutex);
}

void console_puts(const char *s)
{
    console_write(s, strlen(s));
}

void console_printf(const char *fmt, ...)
{
    va_list ap;
    int n;
    int running = scheduler_running();

    if (running) {
        (void)xSemaphoreTake(s_mutex, portMAX_DELAY);
    }
    va_start(ap, fmt);
    n = vsnprintf(s_line, sizeof s_line, fmt, ap);
    va_end(ap);
    if (n > 0) {
        size_t len = ((size_t)n < sizeof s_line) ? (size_t)n : sizeof s_line - 1U;
        if (running) {
            ring_write(s_line, len);
        } else {
            polled_write(s_line, len);
        }
    }
    if (running) {
        (void)xSemaphoreGive(s_mutex);
    }
}

void console_panic_write(const char *s)
{
    if ((CONSOLE_USART->CR1 & USART_CR1_UE) == 0U) {
        return; /* console not initialised yet: TXE would never set */
    }
    CONSOLE_USART->CR1 &= ~USART_CR1_TXEIE;
    polled_write(s, strlen(s));
    while ((CONSOLE_USART->SR & USART_SR_TC) == 0U) {
    }
}

void console_uart_irq(void)
{
    uint32_t sr = CONSOLE_USART->SR;

    if ((sr & USART_SR_TXE) != 0U && (CONSOLE_USART->CR1 & USART_CR1_TXEIE) != 0U) {
        uint32_t tail = s_tx_tail;
        if (tail != s_tx_head) {
            CONSOLE_USART->DR = s_tx_buf[tail & (CONSOLE_TX_BUF_SIZE - 1U)];
            s_tx_tail = tail + 1U;
        } else {
            CONSOLE_USART->CR1 &= ~USART_CR1_TXEIE;
        }
    }
    /* RX is not used yet (console commands arrive in M2). Clear overrun so a
       stray byte cannot leave the IRQ pending: read SR then DR. */
    if ((sr & (USART_SR_ORE | USART_SR_RXNE)) != 0U) {
        (void)CONSOLE_USART->DR;
    }
}
