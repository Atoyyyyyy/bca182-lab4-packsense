"""Window features (PRD 7.2). src/dsp/features.c computes the same 42 values
in the same order (float32 there, float64 here); golden vectors check it.

Definitions (N = 200 samples, population statistics):
  mean, std = sqrt(mean((x - mean)^2)), min, max, range = max - min,
  rms = sqrt(mean(x^2)), mcr = sign changes of (x - mean) / (N - 1)
  |a|, |g|        vector magnitudes per sample
  jerk_peak       max |a[i] - a[i-1]| over |a|, m/s^2 per sample
  peaks_1p5g      upward crossings of |a| through 1.5 g
  e_low, e_high   log10(1e-6 + sum |X_k|^2 / N^2) of |a| - mean(|a|),
                  X = 256-point DFT (zero padded), bins 2..7 and 13..128
  tilt_change_deg angle between mean (ax, ay, az) of the first and last 50
  als_*           of L = log10(1 + lux): mean, max - min,
                  mean(last 50) - mean(first 50)
  ps_max          max PS count over valid samples (0 if none)
  ps_near_frac    valid samples with PS > PS_NEAR, over N
"""
import numpy as np

from common import FEATURES, HIGH_BINS, LOW_BINS, NFFT, PEAK_THRESH, PS_NEAR, TILT_N, WIN


def _stats7(x):
    m = x.mean()
    d = x - m
    std = np.sqrt((d * d).mean())
    mcr = np.count_nonzero(d[1:] * d[:-1] < 0) / (len(x) - 1)
    return [m, std, x.min(), x.max(), x.max() - x.min(), np.sqrt((x * x).mean()), mcr]


def _stats3(x):
    m = x.mean()
    d = x - m
    return [m, np.sqrt((d * d).mean()), x.max() - x.min()]


def window_features(w):
    """w: array [WIN, 9] in common.CH order -> 42 features (float64)."""
    w = np.asarray(w, dtype=np.float64)
    assert w.shape == (WIN, 9)
    ax, ay, az, gx, gy, gz, als, ps, psv = (w[:, i] for i in range(9))
    amag = np.sqrt(ax * ax + ay * ay + az * az)
    gmag = np.sqrt(gx * gx + gy * gy + gz * gz)

    f = _stats7(amag) + _stats7(gmag)
    for x in (ax, ay, az, gx, gy, gz):
        f += _stats3(x)

    jerk = np.abs(np.diff(amag)).max()
    above = amag > PEAK_THRESH
    peaks = np.count_nonzero(above[1:] & ~above[:-1])
    spec = np.fft.rfft(amag - amag.mean(), NFFT)
    p = (spec.real ** 2 + spec.imag ** 2) / (WIN * WIN)
    e_low = np.log10(1e-6 + p[LOW_BINS[0]:LOW_BINS[1] + 1].sum())
    e_high = np.log10(1e-6 + p[HIGH_BINS[0]:HIGH_BINS[1] + 1].sum())
    u = np.array([ax[:TILT_N].mean(), ay[:TILT_N].mean(), az[:TILT_N].mean()])
    v = np.array([ax[-TILT_N:].mean(), ay[-TILT_N:].mean(), az[-TILT_N:].mean()])
    nu, nv = np.linalg.norm(u), np.linalg.norm(v)
    tilt = 0.0
    if nu > 1e-3 and nv > 1e-3:
        tilt = np.degrees(np.arccos(np.clip(u.dot(v) / (nu * nv), -1.0, 1.0)))

    L = np.log10(1.0 + np.maximum(als, 0.0))
    valid = psv > 0.5
    ps_max = ps[valid].max() if valid.any() else 0.0
    near = np.count_nonzero(valid & (ps > PS_NEAR)) / WIN

    f += [jerk, peaks, e_low, e_high, tilt,
          L.mean(), L.max() - L.min(), L[-TILT_N:].mean() - L[:TILT_N].mean(),
          ps_max, near]
    assert len(f) == len(FEATURES)
    return np.array(f, dtype=np.float64)


def features_matrix(windows):
    return np.stack([window_features(w) for w in windows]) if len(windows) else np.zeros((0, len(FEATURES)))
