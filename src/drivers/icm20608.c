/*
 * icm20608.c: ICM-20608-G driver.
 *
 * Sources:
 *  - ICM-20608-G datasheet DS-000081 rev 1.0 (docs/hardware/vendor/, fetched by
 *    tools/fetch_hw_docs.py): accel sensitivity 8192 LSB/g at AFS_SEL=1 (+-4 g),
 *    gyro 65.5 LSB/(deg/s) at FS_SEL=1 (+-500 dps), temperature 326.8 LSB/degC,
 *    PWR_MGMT_1 = 0x6B, PWR_MGMT_2 = 0x6C, ACCEL_CONFIG2 = 0x1D, start-up < 10 ms
 *    for register access.
 *  - The register addresses not printed in that datasheet (SMPLRT_DIV 0x19,
 *    CONFIG 0x1A, GYRO_CONFIG 0x1B, ACCEL_CONFIG 0x1C, ACCEL_XOUT_H 0x3B,
 *    WHO_AM_I 0x75 = 0xAF) come from the ICM-20608-G register map (RM-000030),
 *    which this repository does not have yet. WHO_AM_I = 0xAF read back on the
 *    board confirms the map; a wrong value makes init fail with ST_NOT_PRESENT.
 *  - Temperature: TEMP_degC = TEMP_OUT / 326.8 + 25 (room-temperature offset 0).
 */
#include "icm20608.h"

#include <math.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "board_pins.h"
#include "drivers/i2c_bus.h"

#define REG_SMPLRT_DIV    0x19U
#define REG_CONFIG        0x1AU
#define REG_GYRO_CONFIG   0x1BU
#define REG_ACCEL_CONFIG  0x1CU
#define REG_ACCEL_CONFIG2 0x1DU
#define REG_ACCEL_XOUT_H  0x3BU
#define REG_PWR_MGMT_1    0x6BU
#define REG_PWR_MGMT_2    0x6CU
#define REG_WHO_AM_I      0x75U

#define WHO_AM_I_ICM20608G 0xAFU

#define PWR1_DEVICE_RESET 0x80U
#define PWR1_CLKSEL_AUTO  0x01U /* PLL when ready, else internal oscillator */

/* Configuration values (bit fields per the register map). */
#define VAL_SMPLRT_DIV    9U    /* 1 kHz internal rate / (1 + 9) = 100 Hz */
#define VAL_CONFIG        0x03U /* DLPF_CFG = 3: gyro bandwidth 41 Hz */
#define VAL_GYRO_CONFIG   0x08U /* FS_SEL = 1 (+-500 dps), FCHOICE_B = 00 */
#define VAL_ACCEL_CONFIG  0x08U /* AFS_SEL = 1 (+-4 g) */
#define VAL_ACCEL_CONFIG2 0x03U /* ACCEL_FCHOICE_B = 0, A_DLPF_CFG = 3: 44.8 Hz */

#define ACCEL_LSB_PER_G   8192.0f
#define GYRO_LSB_PER_DPS  65.5f
#define TEMP_LSB_PER_C    326.8f
#define STANDARD_GRAVITY  9.80665f

static icm_info_t s_info;

static status_t wr(uint8_t reg, uint8_t val)
{
    return i2c_write_reg(I2C_BUS_MAIN, I2C_ADDR_ICM20608, reg, val);
}

static status_t rd(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_read_regs(I2C_BUS_MAIN, I2C_ADDR_ICM20608, reg, buf, len);
}

status_t icm20608_init(void)
{
    static const struct {
        uint8_t reg, val;
    } cfg[] = {
        {REG_PWR_MGMT_2, 0x00U}, /* all accel and gyro axes on */
        {REG_SMPLRT_DIV, VAL_SMPLRT_DIV},
        {REG_CONFIG, VAL_CONFIG},
        {REG_GYRO_CONFIG, VAL_GYRO_CONFIG},
        {REG_ACCEL_CONFIG, VAL_ACCEL_CONFIG},
        {REG_ACCEL_CONFIG2, VAL_ACCEL_CONFIG2},
    };
    uint8_t v = 0U;
    status_t st;

    s_info.initialised = false;
    st = rd(REG_WHO_AM_I, &v, 1U);
    s_info.who_am_i = v;
    if (st != ST_OK) {
        s_info.last_status = st;
        return st;
    }
    if (v != WHO_AM_I_ICM20608G) {
        s_info.last_status = ST_NOT_PRESENT;
        return ST_NOT_PRESENT;
    }

    st = wr(REG_PWR_MGMT_1, PWR1_DEVICE_RESET);
    if (st == ST_OK) {
        vTaskDelay(pdMS_TO_TICKS(100)); /* reset, then register access < 10 ms */
        st = wr(REG_PWR_MGMT_1, PWR1_CLKSEL_AUTO);
    }
    if (st == ST_OK) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    for (size_t i = 0; st == ST_OK && i < sizeof cfg / sizeof cfg[0]; i++) {
        st = wr(cfg[i].reg, cfg[i].val);
    }
    /* Read the configuration back: catches a write that silently failed. */
    for (size_t i = 0; st == ST_OK && i < sizeof cfg / sizeof cfg[0]; i++) {
        st = rd(cfg[i].reg, &v, 1U);
        if (st == ST_OK && v != cfg[i].val) {
            st = ST_RANGE_ERR;
        }
    }
    if (st == ST_OK) {
        vTaskDelay(pdMS_TO_TICKS(20)); /* let the filters settle a little */
        s_info.initialised = true;
    }
    s_info.last_status = st;
    return st;
}

static int16_t be16(const uint8_t *p)
{
    return (int16_t)(((uint16_t)p[0] << 8) | p[1]);
}

status_t icm20608_read(icm_sample_t *out)
{
    uint8_t b[14];
    status_t st;

    if (!s_info.initialised) {
        return ST_NOT_PRESENT;
    }
    st = rd(REG_ACCEL_XOUT_H, b, sizeof b);
    s_info.reads++;
    s_info.last_status = st;
    if (st != ST_OK) {
        s_info.errors++;
        return st;
    }

    const int16_t raw[7] = {be16(&b[0]), be16(&b[2]), be16(&b[4]), be16(&b[6]),
                            be16(&b[8]), be16(&b[10]), be16(&b[12])};
    const float a_scale = STANDARD_GRAVITY / ACCEL_LSB_PER_G;

    out->t_ms = (uint32_t)xTaskGetTickCount();
    out->ax = (float)raw[0] * a_scale;
    out->ay = (float)raw[1] * a_scale;
    out->az = (float)raw[2] * a_scale;
    out->temp_c = (float)raw[3] / TEMP_LSB_PER_C + 25.0f;
    out->gx = (float)raw[4] / GYRO_LSB_PER_DPS - s_info.bias_gx;
    out->gy = (float)raw[5] / GYRO_LSB_PER_DPS - s_info.bias_gy;
    out->gz = (float)raw[6] / GYRO_LSB_PER_DPS - s_info.bias_gz;
    out->saturated = false;
    for (int i = 0; i < 7; i++) {
        if (i != 3 && (raw[i] == INT16_MAX || raw[i] == INT16_MIN)) {
            out->saturated = true;
        }
    }
    return out->saturated ? ST_RANGE_ERR : ST_OK;
}

status_t icm20608_calibrate_gyro(uint32_t samples, float *mean_accel_norm)
{
    float sx = 0.0f, sy = 0.0f, sz = 0.0f, sa = 0.0f; /* <= a few hundred samples */
    uint32_t n = 0U;
    icm_sample_t s;
    TickType_t wake = xTaskGetTickCount();

    if (!s_info.initialised) {
        return ST_NOT_PRESENT;
    }
    s_info.bias_gx = s_info.bias_gy = s_info.bias_gz = 0.0f;
    for (uint32_t i = 0; i < samples; i++) {
        (void)xTaskDelayUntil(&wake, pdMS_TO_TICKS(10));
        status_t st = icm20608_read(&s);
        if (st != ST_OK) {
            return st;
        }
        sx += s.gx;
        sy += s.gy;
        sz += s.gz;
        sa += sqrtf(s.ax * s.ax + s.ay * s.ay + s.az * s.az);
        n++;
    }
    if (n == 0U) {
        return ST_RANGE_ERR;
    }
    s_info.bias_gx = sx / (float)n;
    s_info.bias_gy = sy / (float)n;
    s_info.bias_gz = sz / (float)n;
    if (mean_accel_norm != NULL) {
        *mean_accel_norm = sa / (float)n;
    }
    return ST_OK;
}

void icm20608_get_info(icm_info_t *out)
{
    *out = s_info;
}
