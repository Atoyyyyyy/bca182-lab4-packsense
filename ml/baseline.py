"""Hand-written threshold baseline (PRD 7.3). src/app/baseline.c applies the
same rules with the thresholds exported to model_params.h.

    std|a| < t_still                                  -> STILL
    else jerk peak of |a| > t_rough                   -> ROUGH
    else als_logrange > t_als or ps_near_frac > t_ps  -> ACCESS
    else                                              -> CARRIED

The four thresholds are tuned on the TRAIN windows only: coordinate search
over quantiles of each feature, maximising macro-F1, three passes.
"""
import numpy as np
from sklearn.metrics import f1_score

from common import FEATURES

KEYS = ("t_still", "t_rough", "t_als", "t_ps")
FEAT = {"t_still": "amag_std", "t_rough": "amag_jerk_peak", "t_als": "als_logrange", "t_ps": "ps_near_frac"}


def predict(F, th):
    F = np.asarray(F)
    c = {k: F[:, FEATURES.index(v)] for k, v in FEAT.items()}
    out = np.full(len(F), 1, dtype=np.int64)  # CARRIED
    access = (c["t_als"] > th["t_als"]) | (c["t_ps"] > th["t_ps"])
    out[access] = 3
    out[c["t_rough"] > th["t_rough"]] = 2
    out[c["t_still"] < th["t_still"]] = 0
    return out


def tune(F, y, passes=3, nq=41):
    F = np.asarray(F)
    cands = {}
    for k, v in FEAT.items():
        col = F[:, FEATURES.index(v)]
        q = np.unique(np.quantile(col, np.linspace(0, 1, nq)))
        cands[k] = np.concatenate([[q[0] - 1e-6], q, [q[-1] + 1e-6]])
    th = {k: float(np.median(cands[k])) for k in KEYS}
    best = f1_score(y, predict(F, th), average="macro", labels=[0, 1, 2, 3], zero_division=0)
    for _ in range(passes):
        for k in KEYS:
            for c in cands[k]:
                trial = dict(th, **{k: float(c)})
                s = f1_score(y, predict(F, trial), average="macro", labels=[0, 1, 2, 3], zero_division=0)
                if s > best + 1e-12:
                    best, th = s, trial
    return th, best
