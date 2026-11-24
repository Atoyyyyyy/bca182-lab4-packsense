#!/usr/bin/env python3
"""T7 input control (PRD 9, AC-5): each key action x10, success and response time.

    python tools/t7_keys.py evidence/m6_keys.log [--md]

Input: the log from `tools/capture_m2.py m6_keys` and its _notes.csv (one row
per requested action: PC start/end, expected action). Board lines used:
  [key] <KEY> press ... edge_to_feedback_ms=N     first EXTI edge -> LCD key echo
  [key] <KEY> release ... action=A edge_to_action_ms=M
                                                  release edge -> action done
                                                  (mode screen drawn, mute shown, ...)
  [ui] ... alarm acknowledged with <KEY> after N ms
An action counts as done if the board reports it inside its step (or up to
1 s after the step ended). Response time: for `ack` the press echo of the
acknowledging press; for the hold/press actions the release -> action time
(a long press can only be recognised at release). AC-5 needs every response
< 150 ms.
"""
import argparse
import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from m6_common import align, notes_path, pct, read_log, read_notes  # noqa: E402

LATE_S = 1.0
LIMIT_MS = 150
ORDER = ("ack", "arm", "disarm", "mute", "unmute", "diag_enter", "diag_exit")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log")
    ap.add_argument("--md", action="store_true", help="Markdown table")
    a = ap.parse_args()
    npath = notes_path(a.log)
    if not os.path.exists(npath):
        sys.exit("no notes file %s (record with tools/capture_m2.py m6_keys)" % npath)
    rows = read_log(a.log)
    presses = [(t, kv) for t, tag, line, kv in rows if tag == "key" and " press " in line]
    releases = [(t, kv) for t, tag, line, kv in rows if tag == "key" and " release " in line]
    acks = [t for t, tag, line, kv in rows if tag == "ui" and "alarm acknowledged" in line]
    notes = [n for n in align(rows, read_notes(npath)) if n["expected"] in ORDER]
    if not notes:
        sys.exit("no T7 steps in %s" % npath)

    per = collections.defaultdict(lambda: {"tried": 0, "ok": 0, "ms": [], "wrong": collections.Counter()})
    for n in notes:
        lo, hi = n["start"], n["end"] + LATE_S
        exp = n["expected"]
        s = per[exp]
        s["tried"] += 1
        if exp == "ack":
            done = [t for t in acks if lo <= t <= hi]
            ps = [kv for t, kv in presses if lo <= t <= hi]
            if done and ps:
                s["ok"] += 1
                s["ms"].append(int(ps[0]["edge_to_feedback_ms"]))
            elif not ps:
                s["wrong"]["no key press"] += 1
            continue
        rel = [kv for t, kv in releases if lo <= t <= hi]
        hit = [kv for kv in rel if kv.get("action") == exp]
        if hit:
            s["ok"] += 1
            s["ms"].append(int(hit[0]["edge_to_action_ms"]))
        else:
            if not rel:
                s["wrong"]["no key press"] += 1
            for kv in rel:
                s["wrong"][kv.get("action", "?")] += 1

    echo = [int(kv["edge_to_feedback_ms"]) for _, kv in presses]
    head = ["action", "done", "median ms", "p95 ms", "max ms", "not done because"]
    out = []
    for act in ORDER:
        if act not in per:
            continue
        s = per[act]
        ms = s["ms"]
        why = ", ".join("%s x%d" % (k, v) for k, v in s["wrong"].items()) or "-"
        out.append([act, "%d/%d" % (s["ok"], s["tried"]), pct(ms, 50) if ms else "-", pct(ms, 95) if ms else "-",
                    max(ms) if ms else "-", why])
    if a.md:
        print("| " + " | ".join(head) + " |\n|" + "---|" * len(head))
        for r in out:
            print("| " + " | ".join(str(x) for x in r) + " |")
    else:
        w = [max(len(str(x)) for x in col) for col in zip(head, *out)]
        for r in [head] + out:
            print("  ".join(str(x).ljust(k) for x, k in zip(r, w)))
    if echo:
        print("\nkey press -> LCD echo, all %d presses: median %d ms, p95 %d ms, max %d ms"
              % (len(echo), pct(echo, 50), pct(echo, 95), max(echo)))
    all_ms = [m for s in per.values() for m in s["ms"]] + echo
    ten = all(per[k]["ok"] >= 10 for k in per)
    fast = bool(all_ms) and max(all_ms) < LIMIT_MS
    print("T7 every action 10/10: %s" % ("PASS" if ten else "FAIL"))
    print("AC-5 every response < %d ms: %s%s" % (LIMIT_MS, "PASS" if fast else "FAIL",
                                               " (max %d ms)" % max(all_ms) if all_ms else " (no timings)"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
