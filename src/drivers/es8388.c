/*
 * es8388.c: ES8388 control-register access over I2C2 (see es8388.h).
 */
#include "es8388.h"

#include "board_pins.h"
#include "drivers/i2c_bus.h"

status_t es8388_dump(uint8_t regs[ES8388_NREGS])
{
    for (uint8_t r = 0; r < ES8388_NREGS; r++) {
        status_t st = i2c_read_regs(I2C_BUS_MAIN, I2C_ADDR_ES8388, r, &regs[r], 1);
        if (st != ST_OK) {
            return st;
        }
    }
    return ST_OK;
}

status_t es8388_rw_test(uint8_t wrote[ES8388_RW_TESTS], uint8_t got[ES8388_RW_TESTS], uint8_t *original,
                        unsigned *bad)
{
    /* 0x15 / 0x2A: alternating bits; 0x00 and 0x3F: all low / all high
       (only bits 5..0 are claimed for this register). */
    static const uint8_t vals[ES8388_RW_TESTS] = {0x15, 0x2A, 0x00, 0x3F};
    *bad = 0U;
    status_t st = i2c_read_regs(I2C_BUS_MAIN, I2C_ADDR_ES8388, ES8388_REG_LOUT1VOL, original, 1);
    if (st != ST_OK) {
        return st;
    }
    for (unsigned i = 0; i < ES8388_RW_TESTS; i++) {
        wrote[i] = vals[i];
        if ((st = i2c_write_reg(I2C_BUS_MAIN, I2C_ADDR_ES8388, ES8388_REG_LOUT1VOL, vals[i])) != ST_OK) {
            break;
        }
        if ((st = i2c_read_regs(I2C_BUS_MAIN, I2C_ADDR_ES8388, ES8388_REG_LOUT1VOL, &got[i], 1)) != ST_OK) {
            break;
        }
        if ((got[i] & 0x3FU) != vals[i]) {
            (*bad)++;
        }
    }
    const status_t rst = i2c_write_reg(I2C_BUS_MAIN, I2C_ADDR_ES8388, ES8388_REG_LOUT1VOL, *original);
    return (st != ST_OK) ? st : rst;
}
