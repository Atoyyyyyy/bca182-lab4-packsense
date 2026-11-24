#!/usr/bin/env python3
"""Timing statistics from a console log (PRD 8.1-8.3, AC-4, AC-5).

    python tools/latency_stats.py evidence/m5_parity.log [more logs...] [--md]

Reads the firmware's own measurements, nothing else:
  [win] ... t_feat_us=  t_inf_us=  lat_ms=   one line per 2 s window (InferTask
        DWT cycle counts; lat_ms = window's last sample -> LED/LCD updated)
  [key] ... edge_to_feedback_ms=             first EXTI edge -> LCD footer echo
  [selftest] time over N runs: ...           `selftest model` summary
Prints n, median, p95 and max per quantity (--md: as a Markdown table).
Lines may carry the "HH:MM:SS.mmm > " prefix of pio device monitor / capture_m2.py.
"""
import argparse
import math
import re
import sys

KV = re.compile(r"(\w+)=([^\s]+)")
PROMPT = re.compile(r"^(> )+")
# A background line can land in the middle of a typed command's echo
# ("dia[win] n=..."), so tagged lines are found anywhere in the text.
TAG = re.compile(r"\[(win|key|selftest)\]")


def pct(v, p):
    """Nearest-rank percentile (same rule as the firmware)."""
    s = sorted(v)
    return s[max(0, math.ceil(len(s) * p / 100.0) - 1)]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("logs", nargs="+")
    ap.add_argument("--md", action="store_true", help="Markdown table")
    a = ap.parse_args()
    series = {"features (us)": [], "MLP inference (us)": [], "window close -> feedback (ms)": [],
              "key edge -> feedback (ms)": []}
    selftest = []
    windows = valid = 0
    for path in a.logs:
        with open(path, encoding="utf-8", errors="replace") as f:
            for raw in f:
                line = raw.split(" > ", 1)[1] if re.match(r"^\d\d:\d\d:\d\d\.\d{3} > ", raw) else raw
                line = PROMPT.sub("", line.strip())  # "> " left by the CLI prompt
                tag = TAG.search(line)
                line = line[tag.start():] if tag else line
                if line.startswith("[win]"):
                    kv = dict(KV.findall(line))
                    if "lat_ms" not in kv:  # line cut off at the end of the log
                        continue
                    windows += 1
                    if kv.get("valid") != "1":
                        continue
                    valid += 1
                    series["features (us)"].append(int(kv["t_feat_us"]))
                    if kv.get("raw", "-") != "-":
                        series["MLP inference (us)"].append(int(kv["t_inf_us"]))
                    series["window close -> feedback (ms)"].append(int(kv["lat_ms"]))
                elif line.startswith("[key]") and "press" in line:
                    kv = dict(KV.findall(line))
                    series["key edge -> feedback (ms)"].append(int(kv["edge_to_feedback_ms"]))
                elif line.startswith("[selftest] time over"):
                    selftest.append(line[len("[selftest] "):])
    if windows == 0 and not series["key edge -> feedback (ms)"] and not selftest:
        sys.exit("no [win], [key] or [selftest] timing lines found")
    print(f"{windows} window lines ({valid} valid) in {len(a.logs)} log(s)")
    rows = [(k, len(v), pct(v, 50), pct(v, 95), max(v)) for k, v in series.items() if v]
    if a.md:
        print("\n| Quantity | n | Median | p95 | Max |\n|---|---|---|---|---|")
        for r in rows:
            print(f"| {r[0]} | {r[1]} | {r[2]} | {r[3]} | {r[4]} |")
    else:
        for r in rows:
            print(f"{r[0]:32s} n={r[1]:5d}  median={r[2]:6d}  p95={r[3]:6d}  max={r[4]:6d}")
    for s in selftest:
        print("selftest:", s)
    lat = series["window close -> feedback (ms)"]
    if lat:
        print(f"AC-4 sense-to-feedback < 500 ms: {'PASS' if max(lat) < 500 else 'FAIL'} (max {max(lat)} ms)")
    keys = series["key edge -> feedback (ms)"]
    if keys:
        print(f"AC-5 button -> response < 150 ms: {'PASS' if max(keys) < 150 else 'FAIL'} (max {max(keys)} ms)")


if __name__ == "__main__":
    main()
