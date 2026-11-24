#!/usr/bin/env python3
"""T1 functional test per class (PRD 9): observed vs expected, per-class accuracy.

    python tools/t1_results.py evidence/m6_t1_*.log [--sheet docs/test_sheets/T1.csv] [--md] [--csv out.csv]

Input: the logs from `tools/capture_m2.py m6_t1_<class>` and the _notes.csv
next to each (one row per trial: PC start/end time, expected class, ok flag).
Only the board's own `[win]` lines are scored, one per 2 s window (0.5 s hop):
  shown state  = state=<smoothed> when unc=0; UNCERTAIN when unc=1 (LCD "?");
                 INVALID when valid=0 (sensor fault); NONE before the first decision.
  STILL, CARRIED (steady): windows that arrived from start + SETTLE_S to the end
                 of the trial; observed = the most frequent shown state.
  ROUGH, ACCESS (events): windows from the start to end + GRACE_S; observed =
                 the expected class if the board showed it (not uncertain) in any
                 of them, else the most frequent shown state.
  false alerts:  in STILL / CARRIED trials, how many times the shown state
                 switched to ROUGH or ACCESS (each would chirp / log an alert).
The same rules are applied to the on-device threshold baseline (base=, one
decision per window, no smoothing) and to the unsmoothed MLP output (raw=).
Trials the operator marked as gone wrong (ok=0) are listed and left out.
"""
import argparse
import collections
import csv
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from m6_common import CLASSES, align, notes_path, read_log, read_notes  # noqa: E402

SETTLE_S = 3.0  # 2 s window + majority-of-3 (1 s) after the trial starts
GRACE_S = 2.0   # an event's windows close up to 2 s after the action ends
STEADY = ("STILL", "CARRIED")
COLS = CLASSES + ("UNCERTAIN", "INVALID", "NONE", "NO_WINDOWS")


def shown_state(kv):
    if kv.get("valid") != "1":
        return "INVALID"
    st = kv.get("state", "-")
    if st == "-":
        return "NONE"
    return "UNCERTAIN" if kv.get("unc") == "1" else st


def plain(field):
    def f(kv):
        if kv.get("valid") != "1":
            return "INVALID"
        v = kv.get(field, "-")
        return "NONE" if v == "-" else v
    return f


def observe(wins, cls, label_of):
    labels = [label_of(kv) for _, kv in wins]
    if not labels:
        return "NO_WINDOWS", 0
    if cls not in STEADY and cls in labels:
        obs = cls
    else:
        obs = collections.Counter(labels).most_common(1)[0][0]
    alerts = 0
    if cls in STEADY:
        prev = None
        for lab in labels:
            if lab in ("ROUGH", "ACCESS") and lab != prev:
                alerts += 1
            prev = lab
    return obs, alerts


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("logs", nargs="+")
    ap.add_argument("--sheet", help="docs/test_sheets/T1.csv: report trials that are missing")
    ap.add_argument("--md", action="store_true", help="Markdown tables")
    ap.add_argument("--csv", help="write the per-trial results to this CSV")
    a = ap.parse_args()

    results = []
    for path in a.logs:
        npath = notes_path(path)
        if not os.path.exists(npath):
            sys.exit("%s: no notes file %s (record with tools/capture_m2.py m6_t1_<class>)" % (path, npath))
        rows = read_log(path)
        wins = [(t, kv) for t, tag, _, kv in rows if tag == "win" and "lat_ms" in kv]
        for n in align(rows, read_notes(npath)):
            cls = n["expected"].upper()
            if not n["trial"] or cls not in CLASSES:
                continue
            lo = n["start"] + (SETTLE_S if cls in STEADY else 0.0)
            hi = n["end"] + (0.0 if cls in STEADY else GRACE_S)
            w = [(t, kv) for t, kv in wins if lo <= t <= hi]
            model, alerts = observe(w, cls, shown_state)
            base, base_alerts = observe(w, cls, plain("base"))
            raw, _ = observe(w, cls, plain("raw"))
            results.append({"trial": n["trial"], "expected": cls, "model": model, "baseline": base, "raw": raw,
                            "false_alerts": alerts, "baseline_false_alerts": base_alerts, "windows": len(w),
                            "ok": n["ok"], "log": os.path.basename(path)})

    if not results:
        sys.exit("no T1 trials found in the notes files")
    used = [r for r in results if r["ok"]]
    dropped = [r for r in results if not r["ok"]]
    seen = collections.Counter(r["trial"] for r in results)
    dup = sorted(k for k, v in seen.items() if v > 1)

    def table(head, rows):
        if a.md:
            print("| " + " | ".join(head) + " |")
            print("|" + "---|" * len(head))
            for r in rows:
                print("| " + " | ".join(str(x) for x in r) + " |")
        else:
            w = [max(len(str(x)) for x in col) for col in zip(head, *rows)] if rows else [len(h) for h in head]
            for r in [head] + rows:
                print("  ".join(str(x).ljust(n) for x, n in zip(r, w)))
        print()

    print("T1: %d trials scored, %d left out (operator marked them as gone wrong)\n" % (len(used), len(dropped)))
    print("Per trial (model = smoothed state shown on the LCD; baseline = on-device threshold rules):\n")
    table(["trial", "expected", "model", "baseline", "raw MLP", "false alerts", "windows"],
          [[r["trial"], r["expected"], r["model"], r["baseline"], r["raw"], r["false_alerts"], r["windows"]]
           for r in used])

    print("Confusion matrix, model (rows = expected, columns = shown):\n")
    cols = [c for c in COLS if c in CLASSES or any(r["model"] == c for r in used)]
    table(["expected \\ shown"] + list(cols),
          [[e] + [sum(1 for r in used if r["expected"] == e and r["model"] == c) for c in cols] for e in CLASSES])

    print("Per-class accuracy (correct / trials):\n")
    rows, macro_m, macro_b = [], [], []
    for e in CLASSES:
        rs = [r for r in used if r["expected"] == e]
        if not rs:
            rows.append([e, 0, "-", "-", "-"])
            continue
        m = sum(r["model"] == e for r in rs)
        b = sum(r["baseline"] == e for r in rs)
        fa = sum(r["false_alerts"] for r in rs) if e in STEADY else "-"
        macro_m.append(m / len(rs))
        macro_b.append(b / len(rs))
        rows.append([e, len(rs), "%d/%d = %.2f" % (m, len(rs), m / len(rs)), "%d/%d = %.2f" % (b, len(rs), b / len(rs)), fa])
    table(["class", "trials", "model", "baseline", "false alerts (model)"], rows)
    if macro_m:
        print("mean per-class accuracy: model %.3f, baseline %.3f" % (sum(macro_m) / len(macro_m),
                                                                      sum(macro_b) / len(macro_b)))
    fa_m = sum(r["false_alerts"] for r in used)
    fa_b = sum(r["baseline_false_alerts"] for r in used)
    n_steady = sum(1 for r in used if r["expected"] in STEADY)
    print("false ROUGH/ACCESS alerts in %d STILL/CARRIED trials: model %d, baseline %d" % (n_steady, fa_m, fa_b))
    counts = collections.Counter(r["expected"] for r in used)
    short = [c for c in CLASSES if counts[c] < 20]
    print("PRD 9 >= 20 independent trials per class: %s" % ("PASS" if not short else
                                                            "FAIL (" + ", ".join("%s %d" % (c, counts[c]) for c in short) + ")"))
    if dropped:
        print("left out (ok=0): " + ", ".join(r["trial"] for r in dropped))
    if dup:
        print("WARNING: trial ids scored more than once: " + ", ".join(dup))
    if a.sheet:
        with open(a.sheet, encoding="utf-8", newline="") as f:
            planned = [r["trial"] for r in csv.DictReader(f)]
        missing = [t for t in planned if t not in seen]
        print("trials in the sheet but not in the logs: %s" % (", ".join(missing) or "none"))
    if a.csv:
        with open(a.csv, "w", encoding="utf-8", newline="") as f:
            w = csv.DictWriter(f, fieldnames=list(results[0]))
            w.writeheader()
            w.writerows(results)
        print("per-trial CSV: %s" % a.csv)
    return 0


if __name__ == "__main__":
    sys.exit(main())
