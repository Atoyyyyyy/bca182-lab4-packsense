/*
 * extflash.c: SPI NOR flash on SPI2, HAL polled transfers, mode 0,
 * SPI2 clock = PCLK1 42 MHz / 4 = 10.5 MHz (well below any W25Q limit).
 */
#include "extflash.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "board_pins.h"

#define CMD_WREN     0x06U
#define CMD_RDSR1    0x05U
#define CMD_READ     0x03U
#define CMD_PP       0x02U
#define CMD_SE4K     0x20U
#define CMD_JEDEC    0x9FU
#define CMD_SFDP     0x5AU
#define CMD_RELEASE  0xABU /* release from power-down */
#define SR1_BUSY     0x01U

#define SECTOR       4096U
#define PAGE         256U
#define SPI_TIMEOUT  100U

static SPI_HandleTypeDef s_spi;
static extflash_info_t s_info;
static uint8_t s_save[SECTOR];
static uint8_t s_page[PAGE];

static inline void cs_low(void)
{
    HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_RESET);
}

static inline void cs_high(void)
{
    HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_SET);
}

static status_t xfer(const uint8_t *tx, uint16_t ntx, uint8_t *rx, uint16_t nrx)
{
    status_t st = ST_OK;
    cs_low();
    if (ntx && HAL_SPI_Transmit(&s_spi, (uint8_t *)tx, ntx, SPI_TIMEOUT) != HAL_OK) {
        st = ST_TIMEOUT;
    }
    if (st == ST_OK && nrx && HAL_SPI_Receive(&s_spi, rx, nrx, SPI_TIMEOUT) != HAL_OK) {
        st = ST_TIMEOUT;
    }
    cs_high();
    return st;
}

static status_t cmd(uint8_t c)
{
    return xfer(&c, 1, NULL, 0);
}

static status_t read_sr1(uint8_t *sr)
{
    const uint8_t c = CMD_RDSR1;
    return xfer(&c, 1, sr, 1);
}

/* Poll BUSY; sleep between polls when the operation is long (erase). */
static status_t wait_ready(uint32_t timeout_ms, bool sleep)
{
    const uint32_t t0 = HAL_GetTick();
    for (;;) {
        uint8_t sr;
        if (read_sr1(&sr) != ST_OK) {
            return ST_TIMEOUT;
        }
        if ((sr & SR1_BUSY) == 0U) {
            return ST_OK;
        }
        if ((HAL_GetTick() - t0) > timeout_ms) {
            return ST_TIMEOUT;
        }
        if (sleep) {
            vTaskDelay(1);
        }
    }
}

static void gpio_init(void)
{
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
}

static void read_sfdp(void)
{
    /* SFDP read: 0x5A, 24-bit address, 1 dummy byte (JESD216). */
    uint8_t hdr[16];
    const uint8_t c0[5] = {CMD_SFDP, 0, 0, 0, 0};
    if (xfer(c0, 5, hdr, sizeof hdr) != ST_OK) {
        return;
    }
    if (!(hdr[0] == 'S' && hdr[1] == 'F' && hdr[2] == 'D' && hdr[3] == 'P')) {
        return;
    }
    s_info.sfdp_ok = true;
    s_info.sfdp_minor = hdr[4];
    s_info.sfdp_major = hdr[5];
    /* Parameter header 0 (bytes 8..15) = basic flash parameter table;
       its pointer is bytes 12..14. Density is DWORD 2 of that table. */
    const uint32_t ptp = (uint32_t)hdr[12] | ((uint32_t)hdr[13] << 8) | ((uint32_t)hdr[14] << 16);
    const uint32_t a = ptp + 4U;
    const uint8_t c1[5] = {CMD_SFDP, (uint8_t)(a >> 16), (uint8_t)(a >> 8), (uint8_t)a, 0};
    uint8_t d[4];
    if (xfer(c1, 5, d, 4) != ST_OK) {
        return;
    }
    const uint32_t dens = (uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
    if ((dens & 0x80000000UL) == 0U) {
        s_info.size_bytes = (dens + 1U) / 8U; /* value = bits - 1 */
    }
}

status_t extflash_init(void)
{
    memset(&s_info, 0, sizeof s_info);
    gpio_init();

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
    s_info.spi_hz = HAL_RCC_GetPCLK1Freq() / 4U;

    (void)cmd(CMD_RELEASE); /* in case an earlier firmware left it powered down */
    vTaskDelay(pdMS_TO_TICKS(1));

    const uint8_t c = CMD_JEDEC;
    if (xfer(&c, 1, s_info.jedec, 3) != ST_OK) {
        return ST_TIMEOUT;
    }
    const uint8_t *j = s_info.jedec;
    if ((j[0] | j[1] | j[2]) == 0x00U || (j[0] & j[1] & j[2]) == 0xFFU) {
        return ST_NOT_PRESENT;
    }
    read_sfdp();
    s_info.initialised = true;
    return ST_OK;
}

void extflash_get_info(extflash_info_t *info)
{
    *info = s_info;
}

status_t extflash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    if (!s_info.initialised) {
        return ST_NOT_PRESENT;
    }
    const uint8_t c[4] = {CMD_READ, (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr};
    status_t st = ST_OK;
    cs_low();
    if (HAL_SPI_Transmit(&s_spi, (uint8_t *)c, 4, SPI_TIMEOUT) != HAL_OK ||
        HAL_SPI_Receive(&s_spi, buf, (uint16_t)len, SPI_TIMEOUT) != HAL_OK) {
        st = ST_TIMEOUT;
    }
    cs_high();
    return st;
}

static status_t erase_sector(uint32_t addr)
{
    if (cmd(CMD_WREN) != ST_OK) {
        return ST_TIMEOUT;
    }
    const uint8_t c[4] = {CMD_SE4K, (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr};
    if (xfer(c, 4, NULL, 0) != ST_OK) {
        return ST_TIMEOUT;
    }
    return wait_ready(1000U, true);
}

static status_t program_page(uint32_t addr, const uint8_t *data)
{
    if (cmd(CMD_WREN) != ST_OK) {
        return ST_TIMEOUT;
    }
    const uint8_t c[4] = {CMD_PP, (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr};
    status_t st = ST_OK;
    cs_low();
    if (HAL_SPI_Transmit(&s_spi, (uint8_t *)c, 4, SPI_TIMEOUT) != HAL_OK ||
        HAL_SPI_Transmit(&s_spi, (uint8_t *)data, PAGE, SPI_TIMEOUT) != HAL_OK) {
        st = ST_TIMEOUT;
    }
    cs_high();
    if (st != ST_OK) {
        return st;
    }
    return wait_ready(20U, false);
}

static uint8_t pattern(uint32_t i)
{
    return (uint8_t)((i * 7U + 0x5AU) ^ (i >> 8));
}

status_t extflash_selftest(extflash_test_t *r)
{
    memset(r, 0, sizeof *r);
    if (!s_info.initialised || s_info.size_bytes < SECTOR) {
        return ST_NOT_PRESENT;
    }
    r->addr = s_info.size_bytes - SECTOR;

    /* 1. Save the sector. */
    status_t st = extflash_read(r->addr, s_save, SECTOR);
    if (st != ST_OK) {
        return st;
    }
    r->was_blank = true;
    for (uint32_t i = 0; i < SECTOR; i++) {
        if (s_save[i] != 0xFFU) {
            r->was_blank = false;
            break;
        }
    }

    /* 2. Erase and check all 0xFF. */
    const uint32_t t0 = HAL_GetTick();
    if ((st = erase_sector(r->addr)) != ST_OK) {
        return st;
    }
    r->erase_ms = HAL_GetTick() - t0;
    for (uint32_t p = 0; p < SECTOR; p += PAGE) {
        if ((st = extflash_read(r->addr + p, s_page, PAGE)) != ST_OK) {
            return st;
        }
        for (uint32_t i = 0; i < PAGE; i++) {
            r->erase_errors += (s_page[i] != 0xFFU);
        }
    }

    /* 3. Program the pattern, 16 pages, and verify. */
    const uint32_t c0 = DWT->CYCCNT;
    for (uint32_t p = 0; p < SECTOR; p += PAGE) {
        for (uint32_t i = 0; i < PAGE; i++) {
            s_page[i] = pattern(p + i);
        }
        if ((st = program_page(r->addr + p, s_page)) != ST_OK) {
            return st;
        }
    }
    r->program_us = (DWT->CYCCNT - c0) / (SystemCoreClock / 1000000U);
    for (uint32_t p = 0; p < SECTOR; p += PAGE) {
        if ((st = extflash_read(r->addr + p, s_page, PAGE)) != ST_OK) {
            return st;
        }
        for (uint32_t i = 0; i < PAGE; i++) {
            r->verify_errors += (s_page[i] != pattern(p + i));
        }
    }

    /* 4. Put the original content back. */
    if ((st = erase_sector(r->addr)) != ST_OK) {
        return st;
    }
    if (!r->was_blank) {
        for (uint32_t p = 0; p < SECTOR; p += PAGE) {
            bool blank_page = true;
            for (uint32_t i = 0; i < PAGE; i++) {
                if (s_save[p + i] != 0xFFU) {
                    blank_page = false;
                    break;
                }
            }
            if (!blank_page && (st = program_page(r->addr + p, &s_save[p])) != ST_OK) {
                return st;
            }
        }
    }
    for (uint32_t p = 0; p < SECTOR; p += PAGE) {
        if ((st = extflash_read(r->addr + p, s_page, PAGE)) != ST_OK) {
            return st;
        }
        for (uint32_t i = 0; i < PAGE; i++) {
            r->restore_errors += (s_page[i] != s_save[p + i]);
        }
    }
    return (r->erase_errors || r->verify_errors || r->restore_errors) ? ST_RANGE_ERR : ST_OK;
}
