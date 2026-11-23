/*
 * aht.h: AHT10 / AHT21 temperature + humidity sensor on the soft I2C bus,
 * address 0x38 (PRD 6.2; pinmap.md notes the schematic says AHT21).
 *
 * One measurement takes ~80 ms; the calling task sleeps (vTaskDelay) while the
 * sensor converts. Read at most once per second (self-heating). Tasks only.
 */
#ifndef AHT_H
#define AHT_H

#include <stdbool.h>
#include <stdint.h>

#include "status.h"

typedef enum {
    AHT_VARIANT_UNKNOWN = 0,
    AHT_VARIANT_AHT2X,   /* 7th byte is a valid CRC-8 */
    AHT_VARIANT_AHT1X,   /* no valid CRC byte (AHT10 sends none) */
} aht_variant_t;

typedef struct {
    uint32_t t_ms;
    float temp_c;
    float rh;            /* %RH */
} aht_sample_t;

typedef struct {
    uint32_t reads;
    uint32_t errors;
    uint32_t crc_errors;
    status_t last_status;
    uint8_t status_byte; /* last status byte: bit 7 busy, bit 3 calibrated */
    aht_variant_t variant;
    bool initialised;
} aht_info_t;

/* Check the calibration bit; send the calibration/init command if needed. */
status_t aht_init(void);

/* Trigger, wait, read. ST_RANGE_ERR if outside -40..85 degC or 0..100 %RH. */
status_t aht_read(aht_sample_t *out);

void aht_get_info(aht_info_t *out);
const char *aht_variant_name(aht_variant_t v);

#endif /* AHT_H */
