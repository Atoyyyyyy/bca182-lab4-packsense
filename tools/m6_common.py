"""Shared parsing for the M6 analysis scripts (t1_results.py, t7_keys.py).

A log line from tools/capture_m2.py looks like
    HH:MM:SS.mmm > [win] n=12 t_end=... state=STILL ...
and its <log>_notes.csv has `trial,start,end,expected,ok` rows (PC times).
Only the firmware's own lines and the operator's notes are used.
"""
import csv
import os
import re

PREFIX = re.compile(r"^(\d\d):(\d\d):(\d\d)\.(\d{3}) > (.*)$")
KV = re.compile(r"(\w+)=([^\s]+)")
# A background line can land in the middle of a typed command's echo
# ("dia[win] n=..."), so tagged lines are found anywhere in the text.
TAG = re.compile(r"\[(win|key|ui|event|mode|fault|sensor|wdg)\]")
CLASSES = ("STILL", "CARRIED", "ROUGH", "ACCESS")


def hms(text):
    """'HH:MM:SS' or 'HH:MM:SS.mmm' -> seconds."""
    h, m, s = text.strip().split(":")
    return int(h) * 3600 + int(m) * 60 + float(s)


def unwrap(times):
    """Make a list of seconds-of-day monotonic across midnight."""
    out, day, prev = [], 0.0, None
    for t in times:
        if prev is not None and t + day < prev - 43200:
            day += 86400.0
        out.append(t + day)
        prev = t + day
    return out


def read_log(path):
    """[(t, tag, line_from_tag, kv)] for every tagged line with a timestamp."""
    rows, raw_t = [], []
    with open(path, encoding="utf-8", errors="replace") as f:
        for raw in f:
            m = PREFIX.match(raw.rstrip("\n"))
            if not m:
                continue
            line = m.group(5)
            tag = TAG.search(line)
            if not tag:
                continue
            line = line[tag.start():]
            raw_t.append(hms("%s:%s:%s.%s" % m.groups()[:4]))
            rows.append((tag.group(1), line, dict(KV.findall(line))))
    return [(t, tag, line, kv) for t, (tag, line, kv) in zip(unwrap(raw_t), rows)]


def notes_path(log_path):
    stem, _ = os.path.splitext(log_path)
    return stem + "_notes.csv"


def read_notes(path):
    """[{trial, start, end, expected, ok}] with times in seconds (same day rule as the log)."""
    with open(path, encoding="utf-8", newline="") as f:
        rows = list(csv.DictReader(f))
    starts = unwrap([hms(r["start"]) for r in rows])
    ends = unwrap([hms(r["end"]) for r in rows])
    out = []
    for r, s, e in zip(rows, starts, ends):
        if e < s:
            e += 86400.0
        out.append({"trial": r.get("trial", "").strip(), "start": s, "end": e,
                    "expected": r["expected"].strip(), "ok": r.get("ok", "1").strip() != "0"})
    return out


def align(log_rows, notes):
    """If the log crossed midnight but the notes did not start on the same
    side, shift the notes by a day so both use the same clock."""
    if not log_rows or not notes:
        return notes
    t0 = log_rows[0][0]
    if notes[0]["start"] < t0 - 43200:
        for n in notes:
            n["start"] += 86400.0
            n["end"] += 86400.0
    return notes


def pct(v, p):
    """Nearest-rank percentile (same rule as the firmware and latency_stats.py)."""
    s = sorted(v)
    return s[max(0, -(-len(s) * p // 100) - 1)]
