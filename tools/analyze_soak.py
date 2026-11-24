#!/usr/bin/env python3
"""Soak / robustness analysis of a console log (PRD 8.5, AC-6, AC-7, AC-8).

    python tools/analyze_soak.py evidence/m6_soak.log [--notes evidence/m6_soak_notes.csv]

Counts, from the firmware's own lines only:
  boots and their reset causes (=== PackSense banner, [boot] reset cause),
  watchdog reports ([wdg]), sensor DEGRADED / recovered lines ([sensor]),
  windows ([win]): total, rejected, missing window numbers, longest silence,
  events ([event]) by type, key presses ([key]), and stack / heap from the
  `diag mem` dumps (lowest HWM per task, heap minimum).
--notes: CSV `start,end,expected` (PC times HH:MM:SS or HH:MM:SS.mmm, expected =
STILL / CARRIED / ROUGH / ACCESS / anything else); tools/capture_m2.py m6_soak and
m6_offline write it (<log>_notes.csv). ROUGH / ACCESS events outside a span
where that state was expected are counted as false alerts.
Needs the "HH:MM:SS.mmm > " prefix (pio device monitor / capture_m2.py) for
durations and --notes.
"""
import argparse
import collections
import csv
import re
import sys

PREFIX = re.compile(r"^(\d\d):(\d\d):(\d\d)\.(\d{3}) > (.*)$")
KV = re.compile(r"(\w+)=([^\s]+)")
PROMPT = re.compile(r"^(> )+")
# A background line can land in the middle of a typed command's echo
# ("dia[win] n=..."), so tagged lines are found anywhere in the text.
TAG = re.compile(r"\[(win|event|key|sensor|wdg|boot|diag mem)\]|=== PackSense")
EVENT_TYPE = re.compile(r" boot=\d+ (\w+) mode=")


def secs(h, m, s, ms):
    return int(h) * 3600 + int(m) * 60 + int(s) + int(ms) / 1000.0


def hms(text):
    """'HH:MM:SS' or 'HH:MM:SS.mmm' -> seconds of the day."""
    h, m, s = text.strip().split(":")
    return int(h) * 3600 + int(m) * 60 + float(s)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log")
    ap.add_argument("--notes", help="CSV start,end,expected (operator annotations)")
    a = ap.parse_args()

    boots, causes, wdg, sensor = 0, [], [], []
    win_n, rejected, missing, last_n = 0, 0, 0, None
    win_t, gap_max = [], 0.0
    events = collections.Counter()
    alerts = []  # (t, type)
    keys = 0
    hwm = {}
    heap_min = None
    t_first = t_last = None
    day = 0.0
    prev_t = None
    with open(a.log, encoding="utf-8", errors="replace") as f:
        for raw in f:
            m = PREFIX.match(raw.rstrip("\n"))
            t = None
            if m:
                t = secs(*m.groups()[:4]) + day
                if prev_t is not None and t < prev_t - 3600:
                    day += 86400.0  # log crossed midnight
                    t += 86400.0
                prev_t = t
                t_first = t if t_first is None else t_first
                t_last = t
                line = m.group(5)
            else:
                line = raw.strip()
            line = PROMPT.sub("", line)  # "> " left by the CLI prompt
            tag = TAG.search(line)
            line = line[tag.start():] if tag else line
            if line.startswith("=== PackSense"):
                boots += 1
                last_n = None
            elif line.startswith("[boot] reset cause:"):
                causes.append(line.split(":", 1)[1].split("(")[0].strip())
            elif line.startswith("[wdg]") and "IWDG started" not in line:
                wdg.append(line)
            elif line.startswith("[sensor]"):
                sensor.append(line)
            elif line.startswith("[win]"):
                kv = dict(KV.findall(line))
                if "lat_ms" not in kv:  # line cut off at the end of the log
                    continue
                n = int(kv["n"])
                win_n += 1
                if kv.get("valid") != "1":
                    rejected += 1
                if last_n is not None and n > last_n + 1:
                    missing += n - last_n - 1
                last_n = n
                if t is not None:
                    if win_t:
                        gap_max = max(gap_max, t - win_t[-1])
                    win_t.append(t)
            elif line.startswith("[event]"):
                m2 = EVENT_TYPE.search(line)
                typ = m2.group(1) if m2 else "?"
                events[typ] += 1
                if typ in ("ROUGH", "ACCESS") and t is not None:
                    alerts.append((t, typ))
            elif line.startswith("[key]") and " press " in line:
                keys += 1
            elif line.startswith("[diag mem]") and "heap_min=" in line:
                v = int(dict(KV.findall(line))["heap_min"])
                heap_min = v if heap_min is None else min(heap_min, v)
            elif line.startswith("[diag mem]"):
                parts = line.split()
                if len(parts) >= 7 and parts[-2].isdigit() and parts[-1].isdigit():
                    name, h = " ".join(parts[2:-4]), int(parts[-2])
                    hwm[name] = min(hwm.get(name, h), h)

    dur = (t_last - t_first) if t_first is not None else None
    print(f"log: {a.log}")
    if dur is not None:
        print(f"duration: {dur / 60:.1f} min ({dur:.0f} s)")
    print(f"boots: {boots}; reset causes: {', '.join(causes) or '-'}; resets during the log: {max(0, boots - 1)}")
    print(f"watchdog reports: {len(wdg)}")
    for w in wdg:
        print("   ", w)
    print(f"windows: {win_n}, rejected {rejected}, missing window numbers {missing}, "
          f"longest gap between window lines {gap_max:.2f} s")
    if dur and win_n:
        print(f"window rate: {win_n / dur:.3f} /s (expected 2.000)")
    print(f"sensor fault lines: {len(sensor)}")
    for s in sensor:
        print("   ", s)
    print("events:", ", ".join(f"{k} {v}" for k, v in sorted(events.items())) or "-")
    print(f"key presses: {keys}")
    if hwm:
        print("lowest stack HWM (words):", ", ".join(f"{k} {v}" for k, v in sorted(hwm.items())))
        low = [k for k, v in hwm.items() if v < 64]
        print(f"AC-9 no task HWM < 64 words: {'PASS' if not low else 'FAIL ' + ', '.join(low)}")
    if heap_min is not None:
        print(f"heap minimum ever free: {heap_min} B")
    if dur is not None:
        ok = boots <= 1 and not wdg and dur >= 15 * 60
        print(f"AC-6 >= 15 min, 0 resets, 0 watchdog: {'PASS' if ok else 'FAIL'}")

    if a.notes:
        spans = []
        with open(a.notes, encoding="utf-8") as f:
            for r in csv.DictReader(f):
                s = hms(r["start"]) + (day if t_first is not None and hms(r["start"]) < t_first - 43200 else 0)
                e = hms(r["end"]) + (day if t_first is not None and hms(r["end"]) < t_first - 43200 else 0)
                spans.append((s, e, r["expected"].strip().upper()))
        false = [(t, typ) for t, typ in alerts
                 if not any(s - 2 <= t <= e + 3 and exp == typ for s, e, exp in spans)]
        print(f"alerts: {len(alerts)}, false alerts (not inside an expected {{ROUGH, ACCESS}} span): {len(false)}")
        for t, typ in false:
            print(f"    {int(t // 3600) % 24:02d}:{int(t // 60) % 60:02d}:{int(t) % 60:02d} {typ}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
