"""Constants shared by the ML scripts. The firmware copies (model_params.h)
are generated from these by export_c.py; do not edit one without the other."""
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# PACKSENSE_DATA / _OUT / _REPORTS override the folders (pipeline tests in a
# scratch directory, so nothing synthetic can end up in data/ or reports/).
DATA = os.environ.get("PACKSENSE_DATA", os.path.join(ROOT, "data"))
OUT = os.environ.get("PACKSENSE_OUT", os.path.join(ROOT, "ml", "out"))              # derived, git-ignored
REPORTS = os.environ.get("PACKSENSE_REPORTS", os.path.join(ROOT, "ml", "reports"))  # committed results

LABELS = ["STILL", "CARRIED", "ROUGH", "ACCESS"]

FS = 100            # Hz
WIN = 200           # samples (2.0 s)
HOP = 50            # samples (0.5 s)
TRIM_S = 1.0        # seconds dropped at each end of a trial
NFFT = 256

# Capture-log scaling (caplog.h): CSV values are exact multiples of these.
ACC_SCALE = 800.0   # counts per m/s^2
GYR_SCALE = 60.0    # counts per deg/s
ALS_LSB = 0.35      # lux per count

G = 9.80665
PEAK_THRESH = 1.5 * G       # |a| peak count threshold, m/s^2
LOW_BINS = (2, 7)           # 0.78..2.73 Hz (k * 100 / 256), inclusive
HIGH_BINS = (13, 128)       # 5.08..50 Hz, inclusive
TILT_N = 50                 # samples averaged at each window end
PS_NEAR = 100               # PS raw count treated as "near"

# Sample flags (sensor_task.h)
SF_IMU_ERR, SF_IMU_SAT, SF_ALS_ERR, SF_ALS_NEW, SF_ENV_ERR, SF_GAP = 1, 2, 4, 8, 16, 32
MAX_INVALID_FRAC = 0.05     # PRD 6.4: > 5 % invalid samples -> window not classified
SAT_RUN = 3                 # saturated this many samples in a row -> invalid
GAP_MS = 30                 # > 3 sample periods between samples -> gap

# Window channel order (raw window arrays: [WIN, 9])
CH = ["ax", "ay", "az", "gx", "gy", "gz", "als", "ps", "ps_valid"]

FEATURES = (
    [f"amag_{s}" for s in ("mean", "std", "min", "max", "range", "rms", "mcr")]
    + [f"gmag_{s}" for s in ("mean", "std", "min", "max", "range", "rms", "mcr")]
    + [f"{ax}_{s}" for ax in ("ax", "ay", "az", "gx", "gy", "gz") for s in ("mean", "std", "range")]
    + ["amag_jerk_peak", "amag_peaks_1p5g", "amag_e_low", "amag_e_high", "tilt_change_deg",
       "als_logmean", "als_logrange", "als_logdelta", "ps_max", "ps_near_frac"]
)
assert len(FEATURES) == 42
