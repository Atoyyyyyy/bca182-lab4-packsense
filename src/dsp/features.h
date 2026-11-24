/*
 * features.h: the 42 window features of PRD 7.2, identical in definition and
 * order to ml/features.py (float32 here, float64 there). ml/export_c.py
 * refuses to export if these constants differ from ml/common.py; golden
 * vectors (`selftest model`) check the values.
 */
#ifndef FEATURES_H
#define FEATURES_H

#include <stdint.h>

#define FEAT_N        42
#define FEAT_WIN      200   /* samples, 2.0 s at 100 Hz */
#define FEAT_NFFT     256   /* DFT length (zero padded) */
#define FEAT_LOW_K0   2     /* 0.78 Hz */
#define FEAT_LOW_K1   7     /* 2.73 Hz */
#define FEAT_HIGH_K0  13    /* 5.08 Hz */
#define FEAT_HIGH_K1  128   /* 50 Hz */
#define FEAT_TILT_N   50
#define FEAT_PS_NEAR  100
#define FEAT_PEAK_G   1.5f
#define FEAT_G        9.80665f

typedef struct {
    float ax[FEAT_WIN], ay[FEAT_WIN], az[FEAT_WIN]; /* m/s^2 */
    float gx[FEAT_WIN], gy[FEAT_WIN], gz[FEAT_WIN]; /* deg/s */
    float als[FEAT_WIN];                            /* lux */
    float ps[FEAT_WIN];                             /* raw count */
    uint8_t ps_valid[FEAT_WIN];
    /* scratch for features_compute(): each caller owns its window, so the
       function is reentrant (InferTask and `selftest model` can overlap) */
    float amag[FEAT_WIN], gmag[FEAT_WIN], tmp[FEAT_WIN];
} feat_window_t;

/* out[FEAT_N] in the order of ml/common.py FEATURES. Uses w->amag/gmag/tmp. */
void features_compute(feat_window_t *w, float out[FEAT_N]);

#endif /* FEATURES_H */
