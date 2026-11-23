/*
 * ap3216c.c: AP3216C driver.
 *
 * Source: AP3216C datasheet rev 0.86 (Lite-On; docs/hardware/vendor/, fetched
 * by tools/fetch_hw_docs.py), "System Register Descriptions":
 *   0x00 System Configuration: 011 = ALS and PS+IR active, 100 = SW reset
 *   0x0A/0x0B IR data: low byte bits 1:0, high byte bits 9:2; 0x0A bit 7 = IR
 *            overflow (IR and PS data invalid)
 *   0x0C/0x0D ALS data: 16-bit, low byte first
 *   0x0E/0x0F PS data: low byte bits 3:0, high byte bits 5:0 (bits 9:4);
 *            bit 7 = object detect, bit 6 = IR overflow
 *   0x10 ALS Configuration bits 5:4 = range; 00 = 20661 lux, 0.35 lux/count
 * The datasheet's electrical table gives 23360 lux and 0.36 lux/count for
 * range 1 instead; this driver uses the register-description values. Either
 * way the absolute lux is only approximate (no glass/window calibration).
 * Typical conversion time: ALS+PS+IR mode 112.5 ms per function, alternating.
 * Registers are read one at a time (the datasheet does not describe
 * auto-increment).
 */
#include "ap3216c.h"

#include "FreeRTOS.h"
#include "task.h"

#include "board_pins.h"
#include "drivers/i2c_bus.h"

#define REG_SYS_CONFIG 0x00U
#define REG_IR_L       0x0AU
#define REG_IR_H       0x0BU
#define REG_ALS_L      0x0CU
#define REG_ALS_H      0x0DU
#define REG_PS_L       0x0EU
#define REG_PS_H       0x0FU
#define REG_ALS_CONFIG 0x10U

#define SYS_MODE_ALS_PS_IR 0x03U
#define SYS_MODE_SW_RESET  0x04U
#define ALS_RANGE_1        0x00U /* bits 5:4 = 00 */
#define LUX_PER_COUNT_R1   0.35f

static ap_info_t s_info;

static status_t wr(uint8_t reg, uint8_t val)
{
    return i2c_write_reg(I2C_BUS_MAIN, I2C_ADDR_AP3216C, reg, val);
}

status_t ap3216c_init(void)
{
    uint8_t v = 0xFFU;
    status_t st;

    s_info.initialised = false;
    st = wr(REG_SYS_CONFIG, SYS_MODE_SW_RESET);
    if (st == ST_OK) {
        vTaskDelay(pdMS_TO_TICKS(15));
        st = wr(REG_ALS_CONFIG, ALS_RANGE_1);
    }
    if (st == ST_OK) {
        st = wr(REG_SYS_CONFIG, SYS_MODE_ALS_PS_IR);
    }
    if (st == ST_OK) {
        st = i2c_read_regs(I2C_BUS_MAIN, I2C_ADDR_AP3216C, REG_SYS_CONFIG, &v, 1U);
        if (st == ST_OK && (v & 0x07U) != SYS_MODE_ALS_PS_IR) {
            st = ST_NOT_PRESENT;
        }
    }
    if (st == ST_OK) {
        /* First complete ALS + PS cycle (2 x 112.5 ms typical) plus margin. */
        vTaskDelay(pdMS_TO_TICKS(250));
        s_info.initialised = true;
    }
    s_info.last_status = st;
    return st;
}

status_t ap3216c_read(ap_sample_t *out)
{
    static const uint8_t regs[6] = {REG_IR_L, REG_IR_H, REG_ALS_L, REG_ALS_H, REG_PS_L, REG_PS_H};
    uint8_t b[6];
    status_t st = ST_OK;

    if (!s_info.initialised) {
        return ST_NOT_PRESENT;
    }
    for (int i = 0; i < 6 && st == ST_OK; i++) {
        st = i2c_read_regs(I2C_BUS_MAIN, I2C_ADDR_AP3216C, regs[i], &b[i], 1U);
    }
    s_info.reads++;
    s_info.last_status = st;
    if (st != ST_OK) {
        s_info.errors++;
        return st;
    }

    out->t_ms = (uint32_t)xTaskGetTickCount();
    out->ir = (uint16_t)(((uint16_t)b[1] << 2) | (b[0] & 0x03U));
    out->lux = (float)(((uint16_t)b[3] << 8) | b[2]) * LUX_PER_COUNT_R1;
    out->ps = (uint16_t)(((uint16_t)(b[5] & 0x3FU) << 4) | (b[4] & 0x0FU));
    out->object_near = (b[4] & 0x80U) != 0U;
    out->ir_overflow = ((b[0] & 0x80U) != 0U) || ((b[4] & 0x40U) != 0U);
    return out->ir_overflow ? ST_RANGE_ERR : ST_OK;
}

void ap3216c_get_info(ap_info_t *out)
{
    *out = s_info;
}
