/*
 * spi2_bus.c: shared SPI2 bus (see spi2_bus.h).
 */
#include "spi2_bus.h"

#include <stdbool.h>

#include "FreeRTOS.h"
#include "semphr.h"

#include "board_pins.h"

static SPI_HandleTypeDef s_spi;
static SemaphoreHandle_t s_mutex;
static StaticSemaphore_t s_mutex_buf;
static bool s_ready;

status_t spi2_bus_init(void)
{
    if (s_ready) {
        return ST_OK;
    }
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_SPI2_CLK_ENABLE();

    /* Both chip selects high before anything else. */
    HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(WIFI_CS_PORT, WIFI_CS_PIN, GPIO_PIN_SET);
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Pin = FLASH_CS_PIN;
    HAL_GPIO_Init(FLASH_CS_PORT, &gpio);
    gpio.Pin = WIFI_CS_PIN;
    HAL_GPIO_Init(WIFI_CS_PORT, &gpio);

    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = SPI2_GPIO_AF;
    gpio.Pin = SPI2_SCK_PIN;
    HAL_GPIO_Init(SPI2_SCK_PORT, &gpio);
    gpio.Pin = SPI2_MOSI_PIN;
    HAL_GPIO_Init(SPI2_MOSI_PORT, &gpio);
    gpio.Pull = GPIO_PULLUP; /* MISO floats while no chip is selected */
    gpio.Pin = SPI2_MISO_PIN;
    HAL_GPIO_Init(SPI2_MISO_PORT, &gpio);

    s_spi.Instance = SPI2;
    s_spi.Init.Mode = SPI_MODE_MASTER;
    s_spi.Init.Direction = SPI_DIRECTION_2LINES;
    s_spi.Init.DataSize = SPI_DATASIZE_8BIT;
    s_spi.Init.CLKPolarity = SPI_POLARITY_LOW;
    s_spi.Init.CLKPhase = SPI_PHASE_1EDGE; /* mode 0 */
    s_spi.Init.NSS = SPI_NSS_SOFT;
    s_spi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
    s_spi.Init.FirstBit = SPI_FIRSTBIT_MSB;
    s_spi.Init.TIMode = SPI_TIMODE_DISABLE;
    s_spi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    s_spi.Init.CRCPolynomial = 7;
    if (HAL_SPI_Init(&s_spi) != HAL_OK) {
        return ST_BUS_ERR;
    }
    s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_buf);
    s_ready = true;
    return ST_OK;
}

SPI_HandleTypeDef *spi2_bus_handle(void)
{
    return &s_spi;
}

uint32_t spi2_bus_hz(void)
{
    return HAL_RCC_GetPCLK1Freq() / 4U;
}

void spi2_take(void)
{
    (void)xSemaphoreTake(s_mutex, portMAX_DELAY);
}

void spi2_give(void)
{
    (void)xSemaphoreGive(s_mutex);
}
