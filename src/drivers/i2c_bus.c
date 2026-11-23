/*
 * i2c_bus.c: hardware I2C2 (interrupt-driven HAL) and a bit-banged bus.
 * Pins and addresses: include/board_pins.h (pinmap.md "I2C buses and devices").
 *
 * Hardware bus: the task starts a HAL *_IT transfer and blocks on task
 * notification slot NOTIFY_IDX_IO; the HAL completion/error callback (I2C2 IRQ,
 * priority IRQ_PRIO_I2C_MAIN >= 5) gives the notification. On a timeout, a bus
 * error, or a bus stuck BUSY, the bus is recovered: peripheral reset, up to 9
 * SCL pulses until a slave releases SDA, a STOP, then re-init.
 *
 * Soft bus: open-drain GPIO with the board's 5.1 kOhm pull-ups, timed with
 * the DWT cycle counter, supports clock stretching (1 ms limit). Interrupts
 * stay enabled: an interrupt only stretches a clock phase, which I2C allows.
 */
#include "i2c_bus.h"

#include <stdbool.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "app_config.h"
#include "board_pins.h"

#define MUTEX_WAIT_MS   100U
#define STRETCH_MAX_US  1000U

static I2C_HandleTypeDef s_hi2c;
static SemaphoreHandle_t s_mutex[I2C_BUS_COUNT];
static StaticSemaphore_t s_mutex_buf[I2C_BUS_COUNT];
static i2c_bus_stats_t s_stats[I2C_BUS_COUNT];

/* Hardware-bus completion, written by the I2C2 ISR. */
static TaskHandle_t volatile s_waiter;
static volatile status_t s_result;

/* ---- timing ------------------------------------------------------------- */

static void dwt_enable(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void delay_us(uint32_t us)
{
    const uint32_t start = DWT->CYCCNT;
    const uint32_t cycles = us * (SystemCoreClock / 1000000U);
    while ((DWT->CYCCNT - start) < cycles) {
    }
}

/* ---- GPIO helpers for open-drain bit-banging ----------------------------- */

static void od_pin(GPIO_TypeDef *port, uint16_t pin)
{
    GPIO_InitTypeDef g = {0};
    port->BSRR = pin; /* released (high) before switching to output */
    g.Pin = pin;
    g.Mode = GPIO_MODE_OUTPUT_OD;
    g.Pull = GPIO_NOPULL; /* external 5.1 kOhm pull-ups */
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(port, &g);
}

static inline void pin_high(GPIO_TypeDef *port, uint16_t pin)
{
    port->BSRR = pin;
}

static inline void pin_low(GPIO_TypeDef *port, uint16_t pin)
{
    port->BSRR = (uint32_t)pin << 16;
}

static inline bool pin_read(GPIO_TypeDef *port, uint16_t pin)
{
    return (port->IDR & pin) != 0U;
}

/* Clock out up to 9 pulses until the slave releases SDA, then a STOP. */
static void bus_clear(GPIO_TypeDef *scl_port, uint16_t scl, GPIO_TypeDef *sda_port, uint16_t sda)
{
    pin_high(sda_port, sda);
    for (int i = 0; i < 9 && !pin_read(sda_port, sda); i++) {
        pin_low(scl_port, scl);
        delay_us(5U);
        pin_high(scl_port, scl);
        delay_us(5U);
    }
    pin_low(sda_port, sda); /* STOP: SDA rises while SCL is high */
    delay_us(5U);
    pin_high(scl_port, scl);
    delay_us(5U);
    pin_high(sda_port, sda);
    delay_us(5U);
}

/* ---- hardware bus (I2C2) -------------------------------------------------- */

static void main_gpio_af(void)
{
    GPIO_InitTypeDef g = {0};
    g.Pin = I2C_MAIN_SCL_PIN;
    g.Mode = GPIO_MODE_AF_OD;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = I2C_MAIN_GPIO_AF;
    HAL_GPIO_Init(I2C_MAIN_SCL_PORT, &g);
    g.Pin = I2C_MAIN_SDA_PIN;
    HAL_GPIO_Init(I2C_MAIN_SDA_PORT, &g);
}

static void main_reinit(void)
{
    s_hi2c.Instance = I2C_MAIN_INSTANCE; /* DeInit dereferences Instance */
    (void)HAL_I2C_DeInit(&s_hi2c);
    od_pin(I2C_MAIN_SCL_PORT, I2C_MAIN_SCL_PIN);
    od_pin(I2C_MAIN_SDA_PORT, I2C_MAIN_SDA_PIN);
    bus_clear(I2C_MAIN_SCL_PORT, I2C_MAIN_SCL_PIN, I2C_MAIN_SDA_PORT, I2C_MAIN_SDA_PIN);
    main_gpio_af();
    /* Reset the peripheral: clears a BUSY flag latched by glitches while the
       pins were reconfigured (STM32F40x errata, I2C analog filter). */
    __HAL_RCC_I2C2_FORCE_RESET();
    __HAL_RCC_I2C2_RELEASE_RESET();

    s_hi2c.Init.ClockSpeed = I2C_MAIN_CLOCK_HZ;
    s_hi2c.Init.DutyCycle = I2C_DUTYCYCLE_2;
    s_hi2c.Init.OwnAddress1 = 0U;
    s_hi2c.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    s_hi2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    s_hi2c.Init.OwnAddress2 = 0U;
    s_hi2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    s_hi2c.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    (void)HAL_I2C_Init(&s_hi2c);
}

static void main_recover(void)
{
    s_stats[I2C_BUS_MAIN].recoveries++;
    main_reinit();
}

static void main_complete_from_isr(status_t st)
{
    BaseType_t woken = pdFALSE;
    TaskHandle_t t = s_waiter;
    s_result = st;
    if (t != NULL) {
        vTaskNotifyGiveIndexedFromISR(t, NOTIFY_IDX_IO, &woken);
    }
    portYIELD_FROM_ISR(woken);
}

void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *h) { (void)h; main_complete_from_isr(ST_OK); }
void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef *h) { (void)h; main_complete_from_isr(ST_OK); }
void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *h) { (void)h; main_complete_from_isr(ST_OK); }
void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *h) { (void)h; main_complete_from_isr(ST_OK); }
void HAL_I2C_AbortCpltCallback(I2C_HandleTypeDef *h) { (void)h; main_complete_from_isr(ST_BUS_ERR); }

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *h)
{
    /* AF = no acknowledge; anything else (BERR, ARLO, OVR) is a bus error. */
    uint32_t e = HAL_I2C_GetError(h);
    main_complete_from_isr(((e & ~HAL_I2C_ERROR_AF) == 0U && (e & HAL_I2C_ERROR_AF) != 0U) ? ST_NACK
                                                                                              : ST_BUS_ERR);
}

void i2c_main_ev_irq(void)
{
    HAL_I2C_EV_IRQHandler(&s_hi2c);
}

void i2c_main_er_irq(void)
{
    HAL_I2C_ER_IRQHandler(&s_hi2c);
}

typedef enum { OP_WRITE, OP_READ, OP_MEM_WRITE, OP_MEM_READ } main_op_t;

static HAL_StatusTypeDef main_start(main_op_t op, uint8_t addr, uint8_t reg, uint8_t *data, size_t len)
{
    const uint16_t a = (uint16_t)(addr << 1);
    switch (op) {
    case OP_WRITE:     return HAL_I2C_Master_Transmit_IT(&s_hi2c, a, data, (uint16_t)len);
    case OP_READ:      return HAL_I2C_Master_Receive_IT(&s_hi2c, a, data, (uint16_t)len);
    case OP_MEM_WRITE: return HAL_I2C_Mem_Write_IT(&s_hi2c, a, reg, I2C_MEMADD_SIZE_8BIT, data, (uint16_t)len);
    case OP_MEM_READ:  return HAL_I2C_Mem_Read_IT(&s_hi2c, a, reg, I2C_MEMADD_SIZE_8BIT, data, (uint16_t)len);
    default:           return HAL_ERROR;
    }
}

/* Caller holds the bus mutex. */
static status_t main_xfer(main_op_t op, uint8_t addr, uint8_t reg, uint8_t *data, size_t len)
{
    i2c_bus_stats_t *stats = &s_stats[I2C_BUS_MAIN];
    stats->transfers++;

    (void)ulTaskNotifyTakeIndexed(NOTIFY_IDX_IO, pdTRUE, 0); /* drop a stale give */
    s_waiter = xTaskGetCurrentTaskHandle();
    s_result = ST_TIMEOUT;

    HAL_StatusTypeDef hs = main_start(op, addr, reg, data, len);
    if (hs == HAL_BUSY) {
        /* Bus or peripheral stuck BUSY (e.g. a reset in mid-transfer): recover once. */
        main_recover();
        hs = main_start(op, addr, reg, data, len);
    }
    if (hs != HAL_OK) {
        s_waiter = NULL;
        stats->bus_errors++;
        main_recover();
        return ST_BUS_ERR;
    }

    status_t st;
    if (ulTaskNotifyTakeIndexed(NOTIFY_IDX_IO, pdTRUE, pdMS_TO_TICKS(I2C_TIMEOUT_MS)) == 0U) {
        st = ST_TIMEOUT;
    } else {
        st = s_result;
    }
    s_waiter = NULL;

    switch (st) {
    case ST_OK:
        break;
    case ST_NACK:
        stats->nacks++;
        break;
    case ST_TIMEOUT:
        stats->timeouts++;
        main_recover();
        break;
    default:
        stats->bus_errors++;
        main_recover();
        break;
    }
    return st;
}

/* ---- soft bus (PE0/PE1) --------------------------------------------------- */

#define SCL_PORT I2C_SOFT_SCL_PORT
#define SCL_PIN  I2C_SOFT_SCL_PIN
#define SDA_PORT I2C_SOFT_SDA_PORT
#define SDA_PIN  I2C_SOFT_SDA_PIN
#define HALF()   delay_us(I2C_SOFT_HALF_PERIOD_US)

/* Release SCL and wait for it to go high (a slave may stretch the clock). */
static bool soft_scl_release(void)
{
    pin_high(SCL_PORT, SCL_PIN);
    const uint32_t start = DWT->CYCCNT;
    const uint32_t limit = STRETCH_MAX_US * (SystemCoreClock / 1000000U);
    while (!pin_read(SCL_PORT, SCL_PIN)) {
        if ((DWT->CYCCNT - start) > limit) {
            return false;
        }
    }
    return true;
}

static bool soft_start(void)
{
    pin_high(SDA_PORT, SDA_PIN);
    if (!soft_scl_release()) {
        return false;
    }
    HALF();
    pin_low(SDA_PORT, SDA_PIN);
    HALF();
    pin_low(SCL_PORT, SCL_PIN);
    HALF();
    return true;
}

static void soft_stop(void)
{
    pin_low(SDA_PORT, SDA_PIN);
    HALF();
    (void)soft_scl_release();
    HALF();
    pin_high(SDA_PORT, SDA_PIN);
    HALF();
}

/* Returns ST_OK on ACK, ST_NACK, or ST_TIMEOUT if SCL stayed low. */
static status_t soft_write_byte(uint8_t b)
{
    for (int i = 0; i < 8; i++) {
        if ((b & 0x80U) != 0U) {
            pin_high(SDA_PORT, SDA_PIN);
        } else {
            pin_low(SDA_PORT, SDA_PIN);
        }
        b = (uint8_t)(b << 1);
        HALF();
        if (!soft_scl_release()) {
            return ST_TIMEOUT;
        }
        HALF();
        pin_low(SCL_PORT, SCL_PIN);
    }
    pin_high(SDA_PORT, SDA_PIN); /* release for the ACK bit */
    HALF();
    if (!soft_scl_release()) {
        return ST_TIMEOUT;
    }
    HALF();
    const bool ack = !pin_read(SDA_PORT, SDA_PIN);
    pin_low(SCL_PORT, SCL_PIN);
    return ack ? ST_OK : ST_NACK;
}

static status_t soft_read_byte(uint8_t *out, bool ack)
{
    uint8_t b = 0U;
    pin_high(SDA_PORT, SDA_PIN);
    for (int i = 0; i < 8; i++) {
        HALF();
        if (!soft_scl_release()) {
            return ST_TIMEOUT;
        }
        HALF();
        b = (uint8_t)((b << 1) | (pin_read(SDA_PORT, SDA_PIN) ? 1U : 0U));
        pin_low(SCL_PORT, SCL_PIN);
    }
    if (ack) {
        pin_low(SDA_PORT, SDA_PIN);
    }
    HALF();
    if (!soft_scl_release()) {
        return ST_TIMEOUT;
    }
    HALF();
    pin_low(SCL_PORT, SCL_PIN);
    pin_high(SDA_PORT, SDA_PIN);
    *out = b;
    return ST_OK;
}

/* One transaction: optional write phase, optional read phase (repeated start
   between them). Caller holds the bus mutex. */
static status_t soft_xfer(uint8_t addr, const uint8_t *wr, size_t wlen, uint8_t *rd, size_t rlen,
                          bool addr_only)
{
    i2c_bus_stats_t *stats = &s_stats[I2C_BUS_SOFT];
    status_t st = ST_OK;
    stats->transfers++;

    if (!soft_start()) {
        st = ST_TIMEOUT;
        goto done;
    }
    if (wlen > 0U || addr_only) {
        st = soft_write_byte((uint8_t)(addr << 1));
        for (size_t i = 0; st == ST_OK && i < wlen; i++) {
            st = soft_write_byte(wr[i]);
        }
        if (st != ST_OK) {
            goto done;
        }
        if (rlen > 0U && !soft_start()) { /* repeated start */
            st = ST_TIMEOUT;
            goto done;
        }
    }
    if (rlen > 0U) {
        st = soft_write_byte((uint8_t)((addr << 1) | 1U));
        for (size_t i = 0; st == ST_OK && i < rlen; i++) {
            st = soft_read_byte(&rd[i], i + 1U < rlen);
        }
    }
done:
    soft_stop();
    if (st == ST_NACK) {
        stats->nacks++;
    } else if (st == ST_TIMEOUT) {
        stats->timeouts++;
        stats->recoveries++;
        bus_clear(SCL_PORT, SCL_PIN, SDA_PORT, SDA_PIN);
    }
    return st;
}

/* ---- public API ------------------------------------------------------------- */

void i2c_bus_init(void)
{
    dwt_enable();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_I2C2_CLK_ENABLE();

    for (int b = 0; b < I2C_BUS_COUNT; b++) {
        s_mutex[b] = xSemaphoreCreateMutexStatic(&s_mutex_buf[b]);
        configASSERT(s_mutex[b] != NULL);
    }

    od_pin(I2C_SOFT_SCL_PORT, I2C_SOFT_SCL_PIN);
    od_pin(I2C_SOFT_SDA_PORT, I2C_SOFT_SDA_PIN);
    bus_clear(SCL_PORT, SCL_PIN, SDA_PORT, SDA_PIN);

    main_reinit();
    HAL_NVIC_SetPriority(I2C_MAIN_EV_IRQn, IRQ_PRIO_I2C_MAIN, 0U);
    HAL_NVIC_SetPriority(I2C_MAIN_ER_IRQn, IRQ_PRIO_I2C_MAIN, 0U);
    HAL_NVIC_EnableIRQ(I2C_MAIN_EV_IRQn);
    HAL_NVIC_EnableIRQ(I2C_MAIN_ER_IRQn);
}

static bool lock(i2c_bus_id_t bus)
{
    configASSERT(bus < I2C_BUS_COUNT);
    return xSemaphoreTake(s_mutex[bus], pdMS_TO_TICKS(MUTEX_WAIT_MS)) == pdTRUE;
}

static void unlock(i2c_bus_id_t bus)
{
    (void)xSemaphoreGive(s_mutex[bus]);
}

status_t i2c_write(i2c_bus_id_t bus, uint8_t addr, const uint8_t *data, size_t len)
{
    status_t st;
    if (!lock(bus)) {
        return ST_TIMEOUT;
    }
    if (bus == I2C_BUS_MAIN) {
        st = main_xfer(OP_WRITE, addr, 0U, (uint8_t *)data, len); /* HAL API is not const */
    } else {
        st = soft_xfer(addr, data, len, NULL, 0U, true);
    }
    unlock(bus);
    return st;
}

status_t i2c_read(i2c_bus_id_t bus, uint8_t addr, uint8_t *data, size_t len)
{
    status_t st;
    if (!lock(bus)) {
        return ST_TIMEOUT;
    }
    if (bus == I2C_BUS_MAIN) {
        st = main_xfer(OP_READ, addr, 0U, data, len);
    } else {
        st = soft_xfer(addr, NULL, 0U, data, len, false);
    }
    unlock(bus);
    return st;
}

status_t i2c_write_reg(i2c_bus_id_t bus, uint8_t addr, uint8_t reg, uint8_t value)
{
    status_t st;
    if (!lock(bus)) {
        return ST_TIMEOUT;
    }
    if (bus == I2C_BUS_MAIN) {
        st = main_xfer(OP_MEM_WRITE, addr, reg, &value, 1U);
    } else {
        const uint8_t buf[2] = {reg, value};
        st = soft_xfer(addr, buf, 2U, NULL, 0U, true);
    }
    unlock(bus);
    return st;
}

status_t i2c_read_regs(i2c_bus_id_t bus, uint8_t addr, uint8_t reg, uint8_t *data, size_t len)
{
    status_t st;
    if (!lock(bus)) {
        return ST_TIMEOUT;
    }
    if (bus == I2C_BUS_MAIN) {
        st = main_xfer(OP_MEM_READ, addr, reg, data, len);
    } else {
        st = soft_xfer(addr, &reg, 1U, data, len, true);
    }
    unlock(bus);
    return st;
}

status_t i2c_probe(i2c_bus_id_t bus, uint8_t addr)
{
    status_t st;
    if (!lock(bus)) {
        return ST_TIMEOUT;
    }
    if (bus == I2C_BUS_MAIN) {
        /* Polled address-only probe; short (one address byte at 400 kHz). */
        s_stats[I2C_BUS_MAIN].transfers++;
        HAL_StatusTypeDef hs = HAL_I2C_IsDeviceReady(&s_hi2c, (uint16_t)(addr << 1), 1U, 2U);
        if (hs == HAL_OK) {
            st = ST_OK;
        } else if (hs == HAL_BUSY) {
            s_stats[I2C_BUS_MAIN].bus_errors++;
            main_recover();
            st = ST_BUS_ERR;
        } else {
            st = ST_NACK; /* not counted as an error: probing absent addresses is normal */
        }
    } else {
        st = soft_xfer(addr, NULL, 0U, NULL, 0U, true);
        if (st == ST_NACK) {
            s_stats[I2C_BUS_SOFT].nacks--; /* same: a probe miss is not an error */
        }
    }
    unlock(bus);
    return st;
}

const char *i2c_bus_name(i2c_bus_id_t bus)
{
    return (bus == I2C_BUS_MAIN) ? "i2c2(PF1/PF0)" : "soft(PE0/PE1)";
}

void i2c_bus_get_stats(i2c_bus_id_t bus, i2c_bus_stats_t *out)
{
    taskENTER_CRITICAL();
    *out = s_stats[bus];
    taskEXIT_CRITICAL();
}
