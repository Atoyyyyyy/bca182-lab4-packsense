/*
 * features.c: window features, mirror of ml/features.py (see the definitions
 * there). The spectrum uses a direct DFT of only the bins that are needed
 * (2..7 and 13..128, about 25k multiply-adds) instead of a full FFT; with
 * zero padding to 256 points it gives the same bins as numpy's rfft.
 */
#include "features.h"

#include <math.h>
#include <stdbool.h>

static float s_cos[FEAT_NFFT], s_sin[FEAT_NFFT];
static bool s_tw_ready;
static float s_amag[FEAT_WIN], s_gmag[FEAT_WIN], s_d[FEAT_WIN];

static void twiddles(void)
{
    for (int k = 0; k < FEAT_NFFT; k++) {
        const double a = 2.0 * 3.14159265358979323846 * (double)k / (double)FEAT_NFFT;
        s_cos[k] = (float)cos(a);
        s_sin[k] = (float)sin(a);
    }
    s_tw_ready = true;
}

static float mean_of(const float *x, int n)
{
    float s = 0.0f;
    for (int i = 0; i < n; i++) {
        s += x[i];
    }
    return s / (float)n;
}

/* mean, std, min, max, range, rms, mean-crossing rate */
static float *stats7(const float *x, float *f)
{
    const float m = mean_of(x, FEAT_WIN);
    float v = 0.0f, sq = 0.0f, mn = x[0], mx = x[0];
    int cross = 0;
    for (int i = 0; i < FEAT_WIN; i++) {
        const float d = x[i] - m;
        v += d * d;
        sq += x[i] * x[i];
        if (x[i] < mn) {
            mn = x[i];
        }
        if (x[i] > mx) {
            mx = x[i];
        }
        if (i > 0 && d * (x[i - 1] - m) < 0.0f) {
            cross++;
        }
    }
    *f++ = m;
    *f++ = sqrtf(v / (float)FEAT_WIN);
    *f++ = mn;
    *f++ = mx;
    *f++ = mx - mn;
    *f++ = sqrtf(sq / (float)FEAT_WIN);
    *f++ = (float)cross / (float)(FEAT_WIN - 1);
    return f;
}

/* mean, std, range */
static float *stats3(const float *x, float *f)
{
    const float m = mean_of(x, FEAT_WIN);
    float v = 0.0f, mn = x[0], mx = x[0];
    for (int i = 0; i < FEAT_WIN; i++) {
        const float d = x[i] - m;
        v += d * d;
        if (x[i] < mn) {
            mn = x[i];
        }
        if (x[i] > mx) {
            mx = x[i];
        }
    }
    *f++ = m;
    *f++ = sqrtf(v / (float)FEAT_WIN);
    *f++ = mx - mn;
    return f;
}

static float band_energy(const float *d, int k0, int k1)
{
    float e = 0.0f;
    for (int k = k0; k <= k1; k++) {
        float re = 0.0f, im = 0.0f;
        unsigned idx = 0U;
        for (int n = 0; n < FEAT_WIN; n++) {
            re += d[n] * s_cos[idx];
            im -= d[n] * s_sin[idx];
            idx = (idx + (unsigned)k) & (FEAT_NFFT - 1U);
        }
        e += re * re + im * im;
    }
    return log10f(1e-6f + e / ((float)FEAT_WIN * (float)FEAT_WIN));
}

static float mean_range(const float *x, int from, int n)
{
    return mean_of(x + from, n);
}

void features_compute(const feat_window_t *w, float out[FEAT_N])
{
    if (!s_tw_ready) {
        twiddles();
    }
    for (int i = 0; i < FEAT_WIN; i++) {
        s_amag[i] = sqrtf(w->ax[i] * w->ax[i] + w->ay[i] * w->ay[i] + w->az[i] * w->az[i]);
        s_gmag[i] = sqrtf(w->gx[i] * w->gx[i] + w->gy[i] * w->gy[i] + w->gz[i] * w->gz[i]);
    }
    float *f = out;
    f = stats7(s_amag, f);
    f = stats7(s_gmag, f);
    f = stats3(w->ax, f);
    f = stats3(w->ay, f);
    f = stats3(w->az, f);
    f = stats3(w->gx, f);
    f = stats3(w->gy, f);
    f = stats3(w->gz, f);

    /* jerk peak, 1.5 g peak count */
    const float thr = FEAT_PEAK_G * FEAT_G;
    float jerk = 0.0f;
    int peaks = 0;
    for (int i = 1; i < FEAT_WIN; i++) {
        const float dj = fabsf(s_amag[i] - s_amag[i - 1]);
        if (dj > jerk) {
            jerk = dj;
        }
        if (s_amag[i] > thr && !(s_amag[i - 1] > thr)) {
            peaks++;
        }
    }
    *f++ = jerk;
    *f++ = (float)peaks;

    const float am = mean_of(s_amag, FEAT_WIN);
    for (int i = 0; i < FEAT_WIN; i++) {
        s_d[i] = s_amag[i] - am;
    }
    *f++ = band_energy(s_d, FEAT_LOW_K0, FEAT_LOW_K1);
    *f++ = band_energy(s_d, FEAT_HIGH_K0, FEAT_HIGH_K1);

    /* tilt change between the mean gravity vectors at both ends */
    const int e0 = FEAT_WIN - FEAT_TILT_N;
    const float ux = mean_range(w->ax, 0, FEAT_TILT_N), uy = mean_range(w->ay, 0, FEAT_TILT_N),
                uz = mean_range(w->az, 0, FEAT_TILT_N);
    const float vx = mean_range(w->ax, e0, FEAT_TILT_N), vy = mean_range(w->ay, e0, FEAT_TILT_N),
                vz = mean_range(w->az, e0, FEAT_TILT_N);
    const float nu = sqrtf(ux * ux + uy * uy + uz * uz), nv = sqrtf(vx * vx + vy * vy + vz * vz);
    float tilt = 0.0f;
    if (nu > 1e-3f && nv > 1e-3f) {
        float c = (ux * vx + uy * vy + uz * vz) / (nu * nv);
        c = c > 1.0f ? 1.0f : (c < -1.0f ? -1.0f : c);
        tilt = acosf(c) * (180.0f / 3.14159265f);
    }
    *f++ = tilt;

    /* light: L = log10(1 + lux) */
    float Ls = 0.0f, Lmin = 0.0f, Lmax = 0.0f, Lfirst = 0.0f, Llast = 0.0f;
    for (int i = 0; i < FEAT_WIN; i++) {
        const float L = log10f(1.0f + (w->als[i] > 0.0f ? w->als[i] : 0.0f));
        Ls += L;
        if (i == 0 || L < Lmin) {
            Lmin = L;
        }
        if (i == 0 || L > Lmax) {
            Lmax = L;
        }
        if (i < FEAT_TILT_N) {
            Lfirst += L;
        }
        if (i >= e0) {
            Llast += L;
        }
    }
    *f++ = Ls / (float)FEAT_WIN;
    *f++ = Lmax - Lmin;
    *f++ = (Llast - Lfirst) / (float)FEAT_TILT_N;

    /* proximity over valid samples */
    float psmax = 0.0f;
    bool any = false;
    int near = 0;
    for (int i = 0; i < FEAT_WIN; i++) {
        if (!w->ps_valid[i]) {
            continue;
        }
        if (!any || w->ps[i] > psmax) {
            psmax = w->ps[i];
        }
        any = true;
        if (w->ps[i] > (float)FEAT_PS_NEAR) {
            near++;
        }
    }
    *f++ = any ? psmax : 0.0f;
    *f++ = (float)near / (float)FEAT_WIN;
}
