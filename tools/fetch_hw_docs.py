#!/usr/bin/env python3
"""Download the RT-Spark hardware documents into docs/hardware/vendor/.

The documents are not committed to git because they have no redistribution
license. This script gets the exact files the pin map was extracted from
and checks each SHA-256, so every team member reads the same revision.

Usage:  python tools/fetch_hw_docs.py [--force]
"""
from __future__ import annotations

import argparse
import hashlib
import sys
import urllib.parse
import urllib.request
from pathlib import Path

REPO_RAW = "https://raw.githubusercontent.com/RT-Thread-Studio/sdk-bsp-stm32f407-spark/main/"

# (local name, path inside the upstream repo, sha256 recorded on 2026-11-22)
DOCS = [
    ("RT-Spark_schematic_V1.0.pdf",
     "docs/星火1号开发板原理图_V1_0.pdf",
     "1442c30c314f6a230f87c8a4ee92c383731de2694737e7e733b11add17457d3f"),
    ("RT-Spark_user_manual_V1.1.0.pdf",
     "docs/星火1号用户手册_V1.1.0.pdf",
     "91cb0b2bb84214df50a77c3f1b9213684c3153db895714d1ff7fdfda3c495750"),
    ("RT-Spark_placement_BOM.html",
     "docs/星火1号开发板_点位&BOM.html",
     "26f1058c7361031648feb592cfee2695ad12896ab0a3311bdf93a490606f5a44"),
    ("ICM-20608_datasheet.pdf",
     "projects/03_driver_axis/packages/icm20608-v1.0.0/icm20608_datasheet.pdf",
     "99d1aa0c8bd00f51b104d4f9fb8328134b24c614dd0d063758989fef42b16973"),
    ("AP3216C_datasheet.pdf",
     "projects/03_driver_als_ps/packages/ap3216c-v2.0.0/ap3216c_datasheet.pdf",
     "7e05f2388d8fb13da08391811655c9ccda24d26caa2c5fd383008bdd592ad86f"),
    ("AHT10_datasheet_v1.1.pdf",
     "projects/03_driver_temp_humi/packages/aht10-v2.1.0/aht10_datasheet_v1_1_181220.pdf",
     "a6055ef378d97eb28c4cd5de23878c7b6ba4633331c65d3dcf39bae66ac4086e"),
]


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--force", action="store_true", help="re-download even if present")
    args = ap.parse_args()

    out_dir = Path(__file__).resolve().parent.parent / "docs" / "hardware" / "vendor"
    out_dir.mkdir(parents=True, exist_ok=True)

    failures = 0
    for name, upstream, expected in DOCS:
        dest = out_dir / name
        if dest.exists() and not args.force and sha256(dest) == expected:
            print(f"ok      {name} (cached)")
            continue
        url = REPO_RAW + urllib.parse.quote(upstream)
        print(f"fetch   {name} ...", end=" ", flush=True)
        try:
            with urllib.request.urlopen(url, timeout=60) as r:
                dest.write_bytes(r.read())
        except Exception as e:  # network errors are reported, not fatal for other files
            print(f"FAILED ({e})")
            failures += 1
            continue
        got = sha256(dest)
        if got != expected:
            print(f"HASH MISMATCH (got {got}); upstream changed, so re-check the pin map")
            failures += 1
        else:
            print("ok")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
