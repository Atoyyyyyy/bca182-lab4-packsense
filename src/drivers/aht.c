/*
 * aht.c: AHT10 / AHT21 driver.
 *
 * Sources:
 *  - AHT10 datasheet v1.1 (Aosong; docs/hardware/vendor/, fetched by
 *    tools/fetch_hw_docs.py): address 0x38; init/calibrate 0xE1 0x08 0x00;
 *    trigger 0xAC 0x33 0x00; soft reset 0xBA; status bit 7 = busy, bit 3 =
 *    calibration enabled; wait >= 75 ms after the trigger; 20-bit raw values;
 *    RH = S_RH / 2^20 * 100 %, T = S_T / 2^20 * 200 - 50 degC.
 *  - AHT2x (schematic part AHT21): same address, trigger and conversion, but
 *    initialisation 0xBE 0x08 0x00 and a 7th byte with CRC-8 (polynomial 0x31,
 *    init 0xFF), per the Aosong AHT20/AHT21 datasheets. Those are not in
 *    docs/hardware/vendor/ yet. The CRC byte is how this driver tells the two
 *    apart; `diag` prints the result so the chip marking can be compared.
 */
#include "aht.h"

#include "FreeRTOS.h"
#include "task.h"

#include "board_pins.h"
#include "drivers/i2c_bus.h"

#define STATUS_BUSY 0x80U
#define STATUS_CAL  0x08U

static aht_info_t s_info;

static status_t cmd3(uint8_t a, uint8_t b, uint8_t c)
{
    const uint8_t buf[3] = {a, b, c};
    return i2c_write(I2C_BUS_SOFT, I2C_ADDR_AHT, buf, sizeof buf);
}

static status_t read_status(uint8_t *st_byte)
{
    return i2c_read(I2C_BUS_SOFT, I2C_ADDR_AHT, st_byte, 1U);
}

static uint8_t crc8(const uint8_t *p, size_t n)
{
    uint8_t crc = 0xFFU;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80U) ? (uint8_t)((crc << 1) ^ 0x31U) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

status_t aht_init(void)
{
    uint8_t sb = 0U;
    status_t st;

    s_info.initialised = false;
    st = read_status(&sb);
    if (st == ST_OK && (sb & STATUS_CAL) == 0U) {
        st = cmd3(0xBEU, 0x08U, 0x00U); /* AHT2x init */
        vTaskDelay(pdMS_TO_TICKS(10));
        if (st == ST_OK) {
            st = read_status(&sb);
        }
    }
    if (st == ST_OK && (sb & STATUS_CAL) == 0U) {
        st = cmd3(0xE1U, 0x08U, 0x00U); /* AHT10 init/calibrate */
        vTaskDelay(pdMS_TO_TICKS(10));
        if (st == ST_OK) {
            st = read_status(&sb);
        }
    }
    s_info.status_byte = sb;
    if (st == ST_OK && (sb & STATUS_CAL) == 0U) {
        st = ST_NOT_PRESENT; /* answers, but never reports calibrated */
    }
    s_info.last_status = st;
    s_info.initialised = (st == ST_OK);
    return st;
}

status_t aht_read(aht_sample_t *out)
{
    uint8_t b[7];
    status_t st;

    if (!s_info.initialised) {
        return ST_NOT_PRESENT;
    }
    s_info.reads++;
    st = cmd3(0xACU, 0x33U, 0x00U);
    if (st == ST_OK) {
        vTaskDelay(pdMS_TO_TICKS(80));
        for (int tries = 0;; tries++) {
            st = i2c_read(I2C_BUS_SOFT, I2C_ADDR_AHT, b, sizeof b);
            if (st != ST_OK || (b[0] & STATUS_BUSY) == 0U) {
                break;
            }
            if (tries >= 4) {
                st = ST_TIMEOUT;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    if (st == ST_OK) {
        s_info.status_byte = b[0];
        const bool crc_ok = crc8(b, 6U) == b[6];
        if (s_info.variant == AHT_VARIANT_UNKNOWN) {
            s_info.variant = crc_ok ? AHT_VARIANT_AHT2X : AHT_VARIANT_AHT1X;
        } else if (s_info.variant == AHT_VARIANT_AHT2X && !crc_ok) {
            s_info.crc_errors++;
            st = ST_CRC_ERR;
        }
    }
    if (st == ST_OK) {
        const uint32_t raw_rh = ((uint32_t)b[1] << 12) | ((uint32_t)b[2] << 4) | ((uint32_t)b[3] >> 4);
        const uint32_t raw_t = (((uint32_t)b[3] & 0x0FU) << 16) | ((uint32_t)b[4] << 8) | b[5];
        out->t_ms = (uint32_t)xTaskGetTickCount();
        out->rh = (float)raw_rh * (100.0f / 1048576.0f);
        out->temp_c = (float)raw_t * (200.0f / 1048576.0f) - 50.0f;
        if (out->temp_c < -40.0f || out->temp_c > 85.0f || out->rh < 0.0f || out->rh > 100.0f) {
            st = ST_RANGE_ERR;
        }
    }
    s_info.last_status = st;
    if (st != ST_OK) {
        s_info.errors++;
    }
    return st;
}

void aht_get_info(aht_info_t *out)
{
    *out = s_info;
}

const char *aht_variant_name(aht_variant_t v)
{
    switch (v) {
    case AHT_VARIANT_AHT2X: return "AHT2x (CRC byte valid)";
    case AHT_VARIANT_AHT1X: return "AHT1x (no CRC byte)";
    default:                return "unknown (no reading yet)";
    }
}
