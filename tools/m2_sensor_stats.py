#!/usr/bin/env python3
"""Summarise sensor streams captured from the PackSense console (M2).

Usage:  python tools/m2_sensor_stats.py evidence/m2_imu.log [more logs ...]

Reads the CSV lines printed by `imu stream`, `als stream` and `env stream`
(with or without the `HH:MM:SS.mmm > ` prefix of the pio monitor time filter).
Each `# <sensor> stream ... period_ms=P` header starts a new stream. For every
stream it reports:
  * sample count, status counts (OK / errors)
  * sampling interval from the firmware's t_ms: mean, median, p95, min, max,
    and the number of gaps > 3 x period (PRD 6.4 counts those as faults)
  * IMU: per-axis mean and standard deviation, mean |a| and its offset from
    standard gravity, gyro mean (residual bias) -- meaningful only if the board
    lay still during the stream
  * ALS: lux / proximity / IR ranges; ENV: temperature and humidity ranges
No third-party packages needed. Exit code 1 if no stream was found.
"""

import math
import re
import statistics
import sys

PREFIX_RE = re.compile(r"^\d{2}:\d{2}:\d{2}\.\d{3} > ")
HEADER_RE = re.compile(r"^# (imu|als|env) stream n=(\d+) period_ms=(\d+)")
G0 = 9.80665

COLS = {
    "imu": ["t_ms", "ax", "ay", "az", "gx", "gy", "gz", "temp_c", "status"],
    "als": ["t_ms", "lux", "ps", "ir", "object", "ir_overflow", "status"],
    "env": ["t_ms", "temp_c", "rh", "status"],
}


def parse(path):
    streams = []
    cur = None
    with open(path, encoding="utf-8", errors="replace") as fp:
        for raw in fp:
            line = PREFIX_RE.sub("", raw.strip())
            m = HEADER_RE.match(line)
            if m:
                cur = {"sensor": m.group(1), "n_req": int(m.group(2)), "period": int(m.group(3)),
                       "rows": [], "file": path}
                streams.append(cur)
                continue
            if cur is None:
                continue
            parts = line.split(",")
            if parts[0] != cur["sensor"] or len(parts) != len(COLS[cur["sensor"]]) + 1:
                continue
            row = dict(zip(COLS[cur["sensor"]], parts[1:]))
            try:
                row["t_ms"] = int(row["t_ms"])
            except ValueError:
                continue
            cur["rows"].append(row)
    return streams


def num(rows, key):
    out = []
    for r in rows:
        try:
            out.append(float(r[key]))
        except (ValueError, KeyError):
            pass
    return out


def pct(values, p):
    s = sorted(values)
    k = (len(s) - 1) * p / 100.0
    lo, hi = math.floor(k), math.ceil(k)
    return s[lo] + (s[hi] - s[lo]) * (k - lo)


def fmt_stats(values, digits=3):
    if not values:
        return "n/a"
    sd = statistics.pstdev(values) if len(values) > 1 else 0.0
    return "mean %.*f sd %.*f min %.*f max %.*f" % (digits, statistics.fmean(values), digits, sd,
                                                    digits, min(values), digits, max(values))


def report(s, idx):
    rows = s["rows"]
    ok = [r for r in rows if r["status"] == "OK"]
    bad = len(rows) - len(ok)
    print("== stream %d: %s (%s), requested n=%d period=%d ms" % (idx, s["sensor"], s["file"], s["n_req"],
                                                                  s["period"]))
    print("   samples %d, status OK %d, not OK %d" % (len(rows), len(ok), bad))
    if len(rows) >= 2:
        dts = [b["t_ms"] - a["t_ms"] for a, b in zip(rows, rows[1:])]
        gaps = sum(1 for d in dts if d > 3 * s["period"])
        print("   interval ms: mean %.2f median %.1f p95 %.1f min %d max %d; gaps > 3x period: %d"
              % (statistics.fmean(dts), statistics.median(dts), pct(dts, 95), min(dts), max(dts), gaps))
        span = rows[-1]["t_ms"] - rows[0]["t_ms"]
        if span > 0:
            print("   effective rate %.2f Hz over %.1f s" % ((len(rows) - 1) * 1000.0 / span, span / 1000.0))
    if s["sensor"] == "imu" and ok:
        for k in ("ax", "ay", "az"):
            print("   %s m/s2: %s" % (k, fmt_stats(num(ok, k))))
        norms = [math.sqrt(float(r["ax"]) ** 2 + float(r["ay"]) ** 2 + float(r["az"]) ** 2) for r in ok]
        mean_n = statistics.fmean(norms)
        print("   |a| m/s2: %s; offset from g0 %+.3f m/s2 (%+.1f %%)"
              % (fmt_stats(norms), mean_n - G0, (mean_n / G0 - 1.0) * 100.0))
        for k in ("gx", "gy", "gz"):
            print("   %s dps: %s" % (k, fmt_stats(num(ok, k))))
        print("   die temp C: %s" % fmt_stats(num(ok, "temp_c"), 2))
    elif s["sensor"] == "als" and ok:
        print("   lux: %s" % fmt_stats(num(ok, "lux"), 2))
        print("   ps:  %s" % fmt_stats(num(ok, "ps"), 1))
        print("   ir:  %s" % fmt_stats(num(ok, "ir"), 1))
    elif s["sensor"] == "env" and ok:
        print("   temp C: %s" % fmt_stats(num(ok, "temp_c"), 2))
        print("   rh %%:   %s" % fmt_stats(num(ok, "rh"), 2))


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    streams = []
    for path in argv[1:]:
        streams.extend(parse(path))
    if not streams:
        print("no '# <sensor> stream' sections found")
        return 1
    for i, s in enumerate(streams, 1):
        report(s, i)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
