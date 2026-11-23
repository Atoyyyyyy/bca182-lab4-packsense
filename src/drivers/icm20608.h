/*
 * icm20608.h: ICM-20608-G 6-axis IMU on I2C2, address 0x68 (PRD 6.2).
 *
 * Configuration: accel +-4 g, gyro +-500 dps, gyro DLPF 41 Hz, accel DLPF
 * 44.8 Hz, output data rate 100 Hz (1 kHz / (1 + SMPLRT_DIV 9)).
 * Output in m/s^2, deg/s and deg C. Call from tasks only.
 */
#ifndef ICM20608_H
#define ICM20608_H

#include <stdbool.h>
#include <stdint.h>

#include "status.h"

typedef struct {
    uint32_t t_ms;       /* tick count when the read finished */
    float ax, ay, az;    /* m/s^2 */
    float gx, gy, gz;    /* deg/s, gyro bias already subtracted */
    float temp_c;        /* die temperature */
    bool saturated;      /* some axis at full scale */
} icm_sample_t;

typedef struct {
    uint32_t reads;
    uint32_t errors;     /* reads that returned != ST_OK */
    status_t last_status;
    uint8_t who_am_i;
    bool initialised;
    float bias_gx, bias_gy, bias_gz; /* deg/s, from icm20608_calibrate_gyro() */
} icm_info_t;

/* Reset and configure. ST_NOT_PRESENT if WHO_AM_I is wrong. Takes ~120 ms. */
status_t icm20608_init(void);

/* Burst-read accel, temperature and gyro (14 bytes from ACCEL_XOUT_H). */
status_t icm20608_read(icm_sample_t *out);

/* Average the gyro over `samples` reads at 100 Hz (board must be still) and
   store it as the bias. Also returns the mean |a| in m/s^2 (expect ~9.81). */
status_t icm20608_calibrate_gyro(uint32_t samples, float *mean_accel_norm);

void icm20608_get_info(icm_info_t *out);

#endif /* ICM20608_H */
