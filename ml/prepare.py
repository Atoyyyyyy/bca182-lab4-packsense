"""Clean the raw captures, cut windows, compute features (PRD 7.1-7.2, M3 gate).

    .venv\\Scripts\\python ml\\prepare.py [--data-dir data] [--split ml/split.yaml]

Reads data/raw/S<nn>.csv for the sessions listed in ml/split.yaml, and per trial:
  * marks invalid samples: IMU read error, timestamp gap (> 30 ms since the
    previous sample), or part of a run of >= 3 saturated samples (PRD 6.4);
  * trims TRIM_S seconds from each end (handling noise at start/stop);
  * cuts 2 s windows with a 0.5 s hop inside the trial only (a window never
    spans two trials, two labels or two splits);
  * drops a window with > 5 % invalid samples or a total span > 2.05 s.
Writes ml/out/windows.npz (raw windows + metadata), ml/out/features.csv and
ml/reports/cleaning.md (what was excluded and why, window counts per class per
split), then checks the gate: >= 30 train and >= 10 test windows per class,
from disjoint sessions. Exit code 2 if the gate is not met.
"""
import argparse
import csv
import os
import sys
from collections import Counter, defaultdict

import numpy as np
import yaml

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import (ACC_SCALE, ALS_LSB, CH, DATA, FEATURES, FS, GAP_MS, GYR_SCALE, HOP, LABELS,  # noqa: E402
                    MAX_INVALID_FRAC, OUT, REPORTS, SAT_RUN, SF_GAP, SF_IMU_ERR, SF_IMU_SAT, TRIM_S, WIN)
from features import features_matrix  # noqa: E402

GATE_TRAIN, GATE_TEST = 30, 10


def load_session(path):
    """-> list of trials: dict(trial, label, t[int], x[n,9] float, flags[int])"""
    trials = {}
    order = []
    with open(path, newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            k = int(r["trial_id"])
            if k not in trials:
                trials[k] = {"trial": k, "label": r["label"], "t": [], "x": [], "flags": []}
                order.append(k)
            tr = trials[k]
            if r["label"] != tr["label"]:
                raise ValueError(f"{path}: trial {k} has two labels")
            tr["t"].append(int(r["t_ms"]))
            # Rebuild the exact stored integers, then scale (same values the C side sees).
            tr["x"].append([round(float(r[c]) * ACC_SCALE) / ACC_SCALE for c in ("ax", "ay", "az")]
                           + [round(float(r[c]) * GYR_SCALE) / GYR_SCALE for c in ("gx", "gy", "gz")]
                           + [round(float(r["als"]) / ALS_LSB) * ALS_LSB, float(int(r["ps"])),
                              float(int(r["ps_valid"]))])
            tr["flags"].append(int(r["flags"]))
    out = []
    for k in order:
        tr = trials[k]
        tr["t"] = np.array(tr["t"], dtype=np.int64)
        tr["x"] = np.array(tr["x"], dtype=np.float64)
        tr["flags"] = np.array(tr["flags"], dtype=np.int64)
        out.append(tr)
    return out


def invalid_mask(t, flags):
    bad = (flags & (SF_IMU_ERR | SF_GAP)) != 0
    dt = np.diff(t, prepend=t[0])
    bad |= dt > GAP_MS
    sat = (flags & SF_IMU_SAT) != 0
    run = 0
    for i in range(len(sat)):
        run = run + 1 if sat[i] else 0
        if run >= SAT_RUN:
            bad[i - run + 1:i + 1] = True
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data-dir", default=DATA)
    ap.add_argument("--split", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "split.yaml"))
    ap.add_argument("--out", default=OUT)
    ap.add_argument("--reports", default=REPORTS)
    a = ap.parse_args()

    with open(a.split, encoding="utf-8") as f:
        split = yaml.safe_load(f)
    train_s, test_s = list(split.get("train") or []), list(split.get("test") or [])
    excluded_trials = set(split.get("exclude_trials") or [])
    if set(train_s) & set(test_s):
        sys.exit(f"split.yaml: sessions in both train and test: {sorted(set(train_s) & set(test_s))}")
    if not train_s or not test_s:
        sys.exit("split.yaml: list at least one train and one test session")

    X, y, sess, trial, t0, spl = [], [], [], [], [], []
    trial_rows, reasons = [], Counter()
    for split_name, sessions in (("train", train_s), ("test", test_s)):
        for s in sessions:
            path = os.path.join(a.data_dir, "raw", f"{s}.csv")
            if not os.path.exists(path):
                sys.exit(f"{path} missing (split.yaml lists {s})")
            for tr in load_session(path):
                key = f"{s}/{tr['trial']}"
                n = len(tr["t"])
                row = {"session": s, "trial": tr["trial"], "label": tr["label"], "split": split_name,
                       "samples": n, "seconds": n / FS, "windows": 0, "excluded": 0, "note": ""}
                if key in excluded_trials:
                    row["note"] = "excluded in split.yaml"
                    reasons["trial excluded in split.yaml"] += 1
                    trial_rows.append(row)
                    continue
                if tr["label"] not in LABELS:
                    row["note"] = f"unknown label {tr['label']}"
                    reasons["unknown label"] += 1
                    trial_rows.append(row)
                    continue
                bad = invalid_mask(tr["t"], tr["flags"])
                keep = (tr["t"] >= tr["t"][0] + TRIM_S * 1000) & (tr["t"] <= tr["t"][-1] - TRIM_S * 1000)
                idx = np.flatnonzero(keep)
                if len(idx) < WIN:
                    row["note"] = "shorter than one window after trimming"
                    reasons["trial too short"] += 1
                    trial_rows.append(row)
                    continue
                i0, i1 = idx[0], idx[-1] + 1
                for st in range(i0, i1 - WIN + 1, HOP):
                    sl = slice(st, st + WIN)
                    span = tr["t"][st + WIN - 1] - tr["t"][st]
                    nbad = int(bad[sl].sum())
                    if nbad > MAX_INVALID_FRAC * WIN:
                        row["excluded"] += 1
                        reasons["> 5 % invalid samples"] += 1
                        continue
                    if span > (WIN - 1) * 1000 / FS + 50:
                        row["excluded"] += 1
                        reasons["timestamp gap (span > 2.04 s)"] += 1
                        continue
                    X.append(tr["x"][sl])
                    y.append(LABELS.index(tr["label"]))
                    sess.append(s)
                    trial.append(tr["trial"])
                    t0.append(int(tr["t"][st]))
                    spl.append(split_name)
                    row["windows"] += 1
                trial_rows.append(row)

    X = np.stack(X) if X else np.zeros((0, WIN, len(CH)))
    y = np.array(y, dtype=np.int64)
    F = features_matrix(X)
    os.makedirs(a.out, exist_ok=True)
    np.savez_compressed(os.path.join(a.out, "windows.npz"), X=X, y=y, session=np.array(sess), trial=np.array(trial),
                        t0=np.array(t0), split=np.array(spl))
    with open(os.path.join(a.out, "features.csv"), "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["split", "session", "trial", "t0_ms", "label"] + FEATURES)
        for i in range(len(y)):
            w.writerow([spl[i], sess[i], trial[i], t0[i], LABELS[y[i]]] + [f"{v:.7g}" for v in F[i]])

    counts = defaultdict(Counter)
    trials_per = defaultdict(set)
    for i in range(len(y)):
        counts[spl[i]][LABELS[y[i]]] += 1
        trials_per[(spl[i], LABELS[y[i]])].add(f"{sess[i]}/{trial[i]}")
    missing = []
    for lab in LABELS:
        if counts["train"][lab] < GATE_TRAIN:
            missing.append(f"{lab}: {counts['train'][lab]} train windows (need {GATE_TRAIN})")
        if counts["test"][lab] < GATE_TEST:
            missing.append(f"{lab}: {counts['test'][lab]} test windows (need {GATE_TEST})")

    os.makedirs(a.reports, exist_ok=True)
    lines = ["# Data cleaning report", "",
             "Generated by `ml/prepare.py` from `data/raw/` and `ml/split.yaml`. Do not edit by hand.", "",
             f"Train sessions: {', '.join(train_s)}. Test sessions: {', '.join(test_s)}.", "",
             f"Windows: {WIN / FS:.1f} s, hop {HOP / FS:.1f} s, {TRIM_S:.1f} s trimmed at each trial end, "
             f"windows never cross trials.", "",
             "## Windows per class", "",
             "| Class | Train windows | Train trials | Test windows | Test trials |",
             "|---|---|---|---|---|"]
    for lab in LABELS:
        lines.append(f"| {lab} | {counts['train'][lab]} | {len(trials_per[('train', lab)])} | "
                     f"{counts['test'][lab]} | {len(trials_per[('test', lab)])} |")
    lines += ["", f"**Gate** (>= {GATE_TRAIN} train and >= {GATE_TEST} test windows per class, disjoint sessions): "
              + ("**met**" if not missing else "**not met**: " + "; ".join(missing)), "",
              "Overlapping windows from one trial are not independent; the trial counts show how many "
              "independent recordings each class has.", "",
              "## Exclusions", ""]
    if reasons:
        lines += ["| Reason | Count |", "|---|---|"] + [f"| {k} | {v} |" for k, v in reasons.most_common()]
    else:
        lines.append("None.")
    lines += ["", "## Per trial", "", "| Session | Trial | Label | Split | Seconds | Windows kept | Windows excluded | Note |",
              "|---|---|---|---|---|---|---|---|"]
    for r in trial_rows:
        lines.append(f"| {r['session']} | {r['trial']} | {r['label']} | {r['split']} | {r['seconds']:.1f} | "
                     f"{r['windows']} | {r['excluded']} | {r['note']} |")
    with open(os.path.join(a.reports, "cleaning.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")

    print(f"{len(y)} windows ({counts['train'].total()} train, {counts['test'].total()} test) from "
          f"{len(trial_rows)} trials; report: {os.path.relpath(os.path.join(a.reports, 'cleaning.md'))}")
    for lab in LABELS:
        print(f"  {lab:8s} train {counts['train'][lab]:5d}  test {counts['test'][lab]:5d}")
    if missing:
        print("GATE NOT MET:\n  " + "\n  ".join(missing))
        sys.exit(2)
    print("gate met")


if __name__ == "__main__":
    main()
