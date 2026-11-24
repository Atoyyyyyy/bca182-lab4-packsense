#!/usr/bin/env python3
"""T3 memory (PRD 8.4, 9, AC-9): Flash / RAM use from a saved build log.

    python tools/size_report.py evidence/size.txt [--md]

Reads the linker's `--print-memory-usage` table (FLASH, RAM = 128 KB SRAM,
CCMRAM = 64 KB core-coupled RAM) and PlatformIO's size summary from the
output of `pio run -v`. AC-9: Flash < 70 % and RAM < 80 %; the check uses the
linker figures (they include the reserved heap / stack sections) and applies
the RAM limit to SRAM and CCM RAM separately. Stack high-water marks (the other
half of AC-9) come from `diag mem`, see tools/analyze_soak.py.
"""
import argparse
import re
import sys

REGION = re.compile(r"^\s*(\w+):\s+(\d+)\s*(B|KB|MB|GB)\s+(\d+)\s*(B|KB|MB|GB)\s+([\d.]+)%")
PIO = re.compile(r"^(RAM|Flash):\s+\[.*\]\s+([\d.]+)% \(used (\d+) bytes from (\d+) bytes\)")
UNIT = {"B": 1, "KB": 1024, "MB": 1024 ** 2, "GB": 1024 ** 3}
LIMIT = {"FLASH": 70.0, "RAM": 80.0, "CCMRAM": 80.0}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build_log")
    ap.add_argument("--md", action="store_true")
    a = ap.parse_args()
    regions, pio, rev = {}, {}, None
    with open(a.build_log, "rb") as f:
        data = f.read()
    # PowerShell `>` writes UTF-16 (Windows PowerShell 5.1) or UTF-8 with a BOM.
    enc = "utf-16" if data[:2] in (b"\xff\xfe", b"\xfe\xff") else "utf-8-sig"
    for line in data.decode(enc, errors="replace").splitlines():
        m = REGION.match(line)
        if m:
            regions[m.group(1)] = (int(m.group(2)) * UNIT[m.group(3)], int(m.group(4)) * UNIT[m.group(5)],
                                   float(m.group(6)))
            continue
        m = PIO.match(line.strip())
        if m:
            pio[m.group(1)] = (int(m.group(3)), int(m.group(4)), float(m.group(2)))
        m = re.match(r"PackSense git revision: (\S+)", line.strip())
        if m:
            rev = m.group(1)
    if not regions:
        sys.exit("no linker memory table found (was the log made with a full link, e.g. after `pio run -t clean`?)")
    rows = []
    for name in ("FLASH", "RAM", "CCMRAM"):
        if name in regions:
            used, size, p = regions[name]
            ok = p < LIMIT[name]
            rows.append([name, used, size, "%.2f %%" % p, "< %d %%" % LIMIT[name], "PASS" if ok else "FAIL"])
    head = ["region", "used (B)", "size (B)", "use", "AC-9 limit", "result"]
    if a.md:
        print("| " + " | ".join(head) + " |\n|" + "---|" * len(head))
        for r in rows:
            print("| " + " | ".join(str(x) for x in r) + " |")
    else:
        w = [max(len(str(x)) for x in col) for col in zip(head, *rows)]
        for r in [head] + rows:
            print("  ".join(str(x).ljust(k) for x, k in zip(r, w)))
    for k, (used, size, p) in pio.items():
        print("PlatformIO summary %s: %d of %d bytes (%.1f %%), without the reserved heap/stack sections"
              % (k, used, size, p))
    if rev:
        print("firmware git revision in the build flags: %s" % rev)
    print("AC-9 Flash < 70 %% and RAM < 80 %%: %s" % ("PASS" if all(r[-1] == "PASS" for r in rows) else "FAIL"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
