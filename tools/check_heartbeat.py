#!/usr/bin/env python3
"""Check an M1 boot log captured with `pio device monitor` (time filter on).

Usage:  python tools/check_heartbeat.py evidence/m1_boot.log

It reports:
  * whether the banner, HAL-init line and "scheduler started" line are present
  * the clock source and PLL settings the firmware read back from RCC
  * the board's tick rate measured against the PC clock: a least-squares fit of
    PC time vs. FreeRTOS tick over all [hb] lines. 1000 ticks per PC second
    means the 168 MHz clock (and so the 8 MHz HSE) is right. An 8-vs-25 MHz
    mistake would show up as a ~3x error; crystal tolerance is ~0.005 %.
  * gaps or repeats in the heartbeat sequence numbers, and resets (banners)

PC timestamps have USB/OS jitter of a few ms. Capture at least 60 s so the fit
is precise. Prints a summary and exits 0 if every check passes, 1 otherwise.
No third-party packages needed.
"""

import re
import sys
from datetime import timedelta

LINE_RE = re.compile(r"^(\d{2}):(\d{2}):(\d{2})\.(\d{3}) > (.*)$")
HB_RE = re.compile(r"\[hb\] n=(\d+) tick=(\d+) led=(on|off) heap_free=(\d+) heap_min=(\d+) hb_hwm=(\d+)")
RCC_RE = re.compile(r"\[boot\] RCC: SWS=(\w+) PLLSRC=(\w+) M=(\d+) N=(\d+) P=(\d+) Q=(\d+)")

MIN_SECONDS = 60.0
TICK_HZ_EXPECTED = 1000.0
TICK_TOLERANCE = 0.001  # 0.1 %: far wider than jitter, far tighter than any wrong-clock error


def parse(path):
    lines = []
    day = timedelta(0)
    prev = None
    with open(path, encoding="utf-8", errors="replace") as fp:
        for raw in fp:
            m = LINE_RE.match(raw.rstrip("\r\n"))
            if not m:
                continue
            h, mi, s, ms, text = m.groups()
            t = timedelta(hours=int(h), minutes=int(mi), seconds=int(s), milliseconds=int(ms))
            if prev is not None and t + day < prev - timedelta(hours=12):
                day += timedelta(days=1)  # capture crossed midnight
            t += day
            prev = t
            lines.append((t.total_seconds(), text.strip()))
    return lines


def fit(xs, ys):
    n = len(xs)
    mx = sum(xs) / n
    my = sum(ys) / n
    sxx = sum((x - mx) ** 2 for x in xs)
    sxy = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    slope = sxy / sxx
    resid = [y - (my + slope * (x - mx)) for x, y in zip(xs, ys)]
    return slope, max(abs(r) for r in resid)


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    lines = parse(argv[1])
    if not lines:
        print("FAIL: no timestamped lines. Capture with: pio device monitor (time filter on)")
        return 1

    texts = [t for _, t in lines]
    ok = True

    def check(name, passed, detail=""):
        nonlocal ok
        ok &= bool(passed)
        print("%-4s %s%s" % ("PASS" if passed else "FAIL", name, (" - " + detail) if detail else ""))

    banners = [t for t in texts if t.startswith("=== PackSense")]
    check("boot banner present", banners, banners[0] if banners else "")
    check("exactly one boot (no resets during capture)", len(banners) == 1, "%d banner(s)" % len(banners))
    check("HAL init line", any(t.startswith("[boot] HAL init OK") for t in texts))
    check("scheduler started", any("scheduler started" in t for t in texts))
    check("no PANIC/FAULT/assert lines", not any(("PANIC" in t or "FAULT" in t or "assert" in t) for t in texts))

    src = next((t for t in texts if t.startswith("[boot] clock source:")), "")
    check("clock source is the HSE crystal", "HSE 8 MHz crystal" in src, src.replace("[boot] clock source: ", ""))
    rcc = next((RCC_RE.search(t) for t in texts if RCC_RE.search(t)), None)
    if rcc:
        sws, pllsrc, m, n, p, q = rcc.groups()
        check("RCC read-back SWS=PLL PLLSRC=HSE M=8 N=336 P=2 Q=7",
              (sws, pllsrc, m, n, p, q) == ("PLL", "HSE", "8", "336", "2", "7"), rcc.group(0))
    else:
        check("RCC read-back line", False)

    hb = []
    for pc_t, text in lines:
        m = HB_RE.search(text)
        if m:
            hb.append((pc_t, int(m.group(1)), int(m.group(2)), m.group(3), int(m.group(4)),
                       int(m.group(5)), int(m.group(6))))
    check("heartbeat lines", len(hb) >= 2, "%d lines" % len(hb))
    if len(hb) < 2:
        return 1

    seq = [h[1] for h in hb]
    missing = sum(b - a - 1 for a, b in zip(seq, seq[1:]) if b > a + 1)
    check("heartbeat sequence has no gaps or repeats", missing == 0 and seq == sorted(set(seq)),
          "n=%d..%d, %d missing" % (seq[0], seq[-1], missing))

    span = hb[-1][0] - hb[0][0]
    check("capture is at least %d s long" % MIN_SECONDS, span >= MIN_SECONDS, "%.1f s of heartbeats" % span)

    pc = [h[0] for h in hb]
    ticks = [h[2] for h in hb]
    rate, worst = fit(pc, ticks)
    err = rate / TICK_HZ_EXPECTED - 1.0
    check("tick rate vs PC clock within 0.1 %", abs(err) <= TICK_TOLERANCE,
          "%.2f ticks/s (%+.3f %%), worst residual %.0f ms" % (rate, err * 100.0, worst))

    dticks = [b - a for a, b in zip(ticks, ticks[1:])]
    check("heartbeat every 1000 ticks", all(d == 1000 for d in dticks),
          "intervals min=%d max=%d" % (min(dticks), max(dticks)))
    # The LED toggles every 500 ms and a line is printed every 2nd toggle, so
    # every line should report the same LED state. The blink itself is checked
    # by eye (1 Hz).
    check("same LED state at every 1 s report", len({h[3] for h in hb}) == 1,
          "states seen: %s" % ",".join(sorted({h[3] for h in hb})))

    # heap_4 reports 0 until its first allocation; main.c initialises it at boot.
    check("heap counters initialised (heap_free > 0)", all(h[4] > 0 and h[5] > 0 for h in hb),
          "heap_free(min seen)=%d B" % min(h[4] for h in hb))

    print("info heap_free(last)=%d B heap_min=%d B heartbeat stack HWM(last)=%d words"
          % (hb[-1][4], min(h[5] for h in hb), hb[-1][6]))
    print("RESULT: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
