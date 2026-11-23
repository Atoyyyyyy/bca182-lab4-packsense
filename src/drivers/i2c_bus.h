/*
 * i2c_bus.h: the board's two I2C buses behind one interface (PRD 6.2).
 *
 *   I2C_BUS_MAIN  hardware I2C2 on PF1 (SCL) / PF0 (SDA), 400 kHz, interrupt
 *                 driven: the calling task blocks on a task notification while
 *                 the transfer runs. ICM-20608-G 0x68, AP3216C 0x1E, ES8388 0x10.
 *   I2C_BUS_SOFT  bit-banged on PE0 (SCL) / PE1 (SDA), ~100 kHz. AHT21 0x38.
 *
 * Each bus has a mutex, so several tasks may use it. Call only from tasks,
 * after the scheduler has started. Addresses are 7-bit.
 */
#ifndef I2C_BUS_H
#define I2C_BUS_H

#include <stddef.h>
#include <stdint.h>

#include "status.h"

typedef enum {
    I2C_BUS_MAIN = 0,
    I2C_BUS_SOFT = 1,
    I2C_BUS_COUNT
} i2c_bus_id_t;

typedef struct {
    uint32_t transfers;
    uint32_t nacks;
    uint32_t timeouts;
    uint32_t bus_errors;
    uint32_t recoveries;
} i2c_bus_stats_t;

/* GPIO + peripheral init and mutex creation. Call once before the scheduler. */
void i2c_bus_init(void);

status_t i2c_write(i2c_bus_id_t bus, uint8_t addr, const uint8_t *data, size_t len);
status_t i2c_read(i2c_bus_id_t bus, uint8_t addr, uint8_t *data, size_t len);

/* Register access (8-bit register address, repeated start before the read). */
status_t i2c_write_reg(i2c_bus_id_t bus, uint8_t addr, uint8_t reg, uint8_t value);
status_t i2c_read_regs(i2c_bus_id_t bus, uint8_t addr, uint8_t reg, uint8_t *data, size_t len);

/* ST_OK if the address acknowledges. */
status_t i2c_probe(i2c_bus_id_t bus, uint8_t addr);

const char *i2c_bus_name(i2c_bus_id_t bus);
void i2c_bus_get_stats(i2c_bus_id_t bus, i2c_bus_stats_t *out);

/* I2C2 event/error IRQ bodies, called from stm32f4xx_it.c. */
void i2c_main_ev_irq(void);
void i2c_main_er_irq(void);

/* Busy-wait for a few microseconds (DWT cycle counter). For bus timing only. */
void delay_us(uint32_t us);

#endif /* I2C_BUS_H */
