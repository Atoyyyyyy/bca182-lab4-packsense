/*
 * ap3216c.h: AP3216C ambient light (ALS) + proximity (PS) + IR sensor on I2C2,
 * address 0x1E (PRD 6.2).
 *
 * Mode: ALS and PS+IR active (continuous). ALS range 1 (0..20661 lux,
 * 0.35 lux/count). One full ALS+PS cycle takes ~225 ms, so reading faster than
 * that returns repeated values. Call from tasks only.
 */
#ifndef AP3216C_H
#define AP3216C_H

#include <stdbool.h>
#include <stdint.h>

#include "status.h"

typedef struct {
    uint32_t t_ms;
    float lux;           /* ambient light */
    uint16_t ps;         /* proximity, 10-bit raw count (higher = closer) */
    uint16_t ir;         /* IR, 10-bit raw count */
    bool ir_overflow;    /* strong IR: PS and IR data invalid */
    bool object_near;    /* PS object-detect flag */
} ap_sample_t;

typedef struct {
    uint32_t reads;
    uint32_t errors;
    status_t last_status;
    bool initialised;
} ap_info_t;

/* Software reset, then ALS+PS+IR continuous mode. ST_NOT_PRESENT if the mode
   register does not read back. */
status_t ap3216c_init(void);

/* Read IR, ALS and PS data registers. Returns ST_RANGE_ERR (with the sample
   filled in) when the IR-overflow flag marks PS/IR invalid. */
status_t ap3216c_read(ap_sample_t *out);

void ap3216c_get_info(ap_info_t *out);

#endif /* AP3216C_H */
