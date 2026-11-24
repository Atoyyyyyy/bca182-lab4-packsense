/*
 * es8388.h: ES8388 audio codec, control port only (I2C2, 0x10).
 * pinmap.md "Audio": the codec is reached only from DIAG; PackSense does not
 * record or store audio (PRD 3).
 *
 * The ES8388 has no chip-ID register, so presence is shown by the I2C ACK,
 * a register dump and a write/read-back test on one harmless register.
 * Register addresses as in the board vendor's drv_es8388.h
 * (sdk-bsp-stm32f407-spark 4c284fc); the ES8388 datasheet is not in
 * docs/hardware/vendor yet.
 */
#ifndef ES8388_H
#define ES8388_H

#include <stdint.h>

#include "status.h"

#define ES8388_NREGS        0x35U /* 0x00 CONTROL1 .. 0x34 DACCONTROL30 */
#define ES8388_REG_DACPOWER 0x04U
#define ES8388_REG_LOUT1VOL 0x2EU /* DACCONTROL24, LOUT1 volume, bits 5..0 */

/* Read all control registers one by one (no burst-read assumption). */
status_t es8388_dump(uint8_t regs[ES8388_NREGS]);

/* Write 4 test values to LOUT1VOL (bits 5..0), read each back, restore the
   original. The headphone output stays powered down (DACPOWER untouched).
   Returns the number of mismatches in *bad. */
#define ES8388_RW_TESTS 4U
status_t es8388_rw_test(uint8_t wrote[ES8388_RW_TESTS], uint8_t got[ES8388_RW_TESTS], uint8_t *original,
                        unsigned *bad);

#endif /* ES8388_H */
