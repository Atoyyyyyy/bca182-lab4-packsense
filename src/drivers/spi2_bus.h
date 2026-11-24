/*
 * spi2_bus.h: the shared SPI2 bus (SPI flash on CS0 PB12, RW007 Wi-Fi on
 * CS1 PF10). One owner for the pins, the HAL handle and a mutex.
 *
 * Mode 0, 8-bit, MSB first, PCLK1 42 MHz / 4 = 10.5 MHz: inside the flash
 * limit and the RW007 setting in the board vendor's driver (15 MHz, module
 * max 30 MHz), so neither device needs the bus reconfigured.
 *
 * A driver takes the bus for one chip-select window: spi2_take() before
 * pulling its CS low, spi2_give() after releasing it.
 */
#ifndef SPI2_BUS_H
#define SPI2_BUS_H

#include <stdint.h>

#include "stm32f4xx_hal.h"

#include "status.h"

/* Pins, both chip selects HIGH, HAL init, mutex. Idempotent; call from a task. */
status_t spi2_bus_init(void);
SPI_HandleTypeDef *spi2_bus_handle(void);
uint32_t spi2_bus_hz(void);

void spi2_take(void);
void spi2_give(void);

#endif /* SPI2_BUS_H */
