/*
 * sdcard.c: microSD on the STM32 SDIO peripheral, HAL polled mode.
 *
 * SDIO kernel clock = PLL48CK = 48 MHz (PLL Q = 7). The HAL identifies the
 * card at 400 kHz; afterwards the bus runs at 48 MHz / (CLKDIV + 2). CLKDIV 2
 * gives 12 MHz, a conservative start until a card has been measured; 4-bit
 * mode after identification. No card answering = ST_NOT_PRESENT (PRD AC-8:
 * "SD absent" must not crash anything).
 */
#include "sdcard.h"

#include <string.h>

#include "board_pins.h"

#define SD_TRANSFER_CLKDIV 2U
#define SD_TIMEOUT_MS      1000U

static SD_HandleTypeDef s_sd;
static sd_info_t s_info;

void sd_early_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOF_CLK_ENABLE();
    gpio.Pin = SD_CD_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(SD_CD_PORT, &gpio);
}

bool sd_cd_pin_high(void)
{
    return HAL_GPIO_ReadPin(SD_CD_PORT, SD_CD_PIN) == GPIO_PIN_SET;
}

static void sdio_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_SDIO_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = SD_GPIO_AF;
    /* Data and command lines idle high (pull-ups, as the SD spec expects). */
    gpio.Pull = GPIO_PULLUP;
    gpio.Pin = SD_DATA_PINS;
    HAL_GPIO_Init(SD_DATA_PORT, &gpio);
    gpio.Pin = SD_CMD_PIN;
    HAL_GPIO_Init(SD_CMD_PORT, &gpio);
    gpio.Pull = GPIO_NOPULL;
    gpio.Pin = SD_CK_PIN;
    HAL_GPIO_Init(SD_CK_PORT, &gpio);
}

static void copy_cid(const HAL_SD_CardCIDTypeDef *cid)
{
    s_info.manufacturer = cid->ManufacturerID;
    s_info.oem[0] = (char)(cid->OEM_AppliID >> 8);
    s_info.oem[1] = (char)(cid->OEM_AppliID & 0xFFU);
    s_info.oem[2] = '\0';
    /* PNM: 5 ASCII chars, ProdName1 holds the first 4, ProdName2 the 5th. */
    s_info.product[0] = (char)(cid->ProdName1 >> 24);
    s_info.product[1] = (char)(cid->ProdName1 >> 16);
    s_info.product[2] = (char)(cid->ProdName1 >> 8);
    s_info.product[3] = (char)(cid->ProdName1);
    s_info.product[4] = (char)(cid->ProdName2);
    s_info.product[5] = '\0';
    for (int i = 0; i < 5; i++) {
        if (s_info.product[i] < 0x20 || s_info.product[i] > 0x7E) {
            s_info.product[i] = '?';
        }
    }
    s_info.serial = cid->ProdSN;
}

status_t sd_init(void)
{
    memset(&s_info, 0, sizeof s_info);
    s_info.cd_pin_high = sd_cd_pin_high();

    if (s_sd.Instance != NULL) { /* a previous attempt: start from scratch */
        (void)HAL_SD_DeInit(&s_sd);
    }
    sdio_gpio_init();

    s_sd.Instance = SDIO;
    s_sd.Init.ClockEdge = SDIO_CLOCK_EDGE_RISING;
    s_sd.Init.ClockBypass = SDIO_CLOCK_BYPASS_DISABLE;
    s_sd.Init.ClockPowerSave = SDIO_CLOCK_POWER_SAVE_DISABLE;
    s_sd.Init.BusWide = SDIO_BUS_WIDE_1B; /* identification is always 1-bit */
    s_sd.Init.HardwareFlowControl = SDIO_HARDWARE_FLOW_CONTROL_DISABLE;
    s_sd.Init.ClockDiv = SD_TRANSFER_CLKDIV;

    if (HAL_SD_Init(&s_sd) != HAL_OK) {
        s_info.hal_error = HAL_SD_GetError(&s_sd);
        (void)HAL_SD_DeInit(&s_sd);
        return ST_NOT_PRESENT;
    }
    if (HAL_SD_ConfigWideBusOperation(&s_sd, SDIO_BUS_WIDE_4B) != HAL_OK) {
        s_info.hal_error = HAL_SD_GetError(&s_sd);
        return ST_BUS_ERR;
    }

    HAL_SD_CardInfoTypeDef ci;
    HAL_SD_CardCIDTypeDef cid;
    (void)HAL_SD_GetCardInfo(&s_sd, &ci);
    (void)HAL_SD_GetCardCID(&s_sd, &cid);
    s_info.card_type = ci.CardType;
    s_info.block_count = (ci.LogBlockSize == 512U) ? ci.LogBlockNbr : (uint32_t)(((uint64_t)ci.LogBlockNbr * ci.LogBlockSize) / 512U);
    copy_cid(&cid);
    s_info.bus_hz = 48000000U / (SD_TRANSFER_CLKDIV + 2U);
    s_info.present = true;
    return ST_OK;
}

void sd_get_info(sd_info_t *info)
{
    s_info.cd_pin_high = sd_cd_pin_high();
    *info = s_info;
}

status_t sd_read_block(uint32_t lba, uint8_t buf[512])
{
    if (!s_info.present) {
        return ST_NOT_PRESENT;
    }
    if (HAL_SD_ReadBlocks(&s_sd, buf, lba, 1, SD_TIMEOUT_MS) != HAL_OK) {
        s_info.hal_error = HAL_SD_GetError(&s_sd);
        return ST_TIMEOUT;
    }
    /* Wait until the card is back in the transfer state. */
    const uint32_t t0 = HAL_GetTick();
    while (HAL_SD_GetCardState(&s_sd) != HAL_SD_CARD_TRANSFER) {
        if ((HAL_GetTick() - t0) > SD_TIMEOUT_MS) {
            return ST_TIMEOUT;
        }
    }
    return ST_OK;
}
