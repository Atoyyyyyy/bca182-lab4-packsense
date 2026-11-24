"""Download labelled trials from the board's capture log (M3, PRD 7.1).

The board records trials to its SPI flash in CAPTURE mode (see
docs/test_sheets/m3_capture.md). This script reads the log over the console,
checks every line's CRC and every record's checksum, splits it by session and
writes, per session:

    data/raw/S<nn>.csv     session_id,trial_id,label,t_ms,ax,ay,az,gx,gy,gz,
                           als,ps,ps_valid,temp,rh,flags
    data/manifest.csv      one row per (session, label)

Session ids (S01, S02, ...) are global: the board numbers its sessions from 1
again after a wipe, so each imported session gets the next free id here, and a
source key stops the same session from being imported twice.

Usage (PowerShell, any Python with pyserial, e.g. PlatformIO's):
    ... tools\\capture.py status
    ... tools\\capture.py dump              (asks for operator/bag/load/location per session)
    ... tools\\capture.py wipe              (after a good dump)
Options: --port COM3, --baud 921600 (dump speed; 0 = stay at 115200),
         --data-dir data, and --operator/--bag/--load/--location/--notes to
         answer the questions from the command line.
"""
import argparse
import base64
import csv
import datetime
import os
import re
import struct
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial missing: use the PlatformIO Python (~/.platformio/penv/Scripts/python.exe)")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LABELS = ["STILL", "CARRIED", "ROUGH", "ACCESS"]
REC = struct.Struct("<I3h3hHHhHBBHHH")  # caplog_rec_t, 32 bytes
SCHEMA = ["session_id", "trial_id", "label", "t_ms", "ax", "ay", "az", "gx", "gy", "gz",
          "als", "ps", "ps_valid", "temp", "rh", "flags"]
MANIFEST = ["session_id", "date", "operator", "bag", "load", "location", "label", "n_trials",
            "seconds", "notes", "source_key"]


def crc16(data):
    c = 0xFFFF
    for b in data:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) if c & 0x8000 else (c << 1)
            c &= 0xFFFF
    return c


def check_ok(raw):
    return (sum(raw[:30]) & 0xFFFF) ^ 0xA5A5 == struct.unpack_from("<H", raw, 30)[0]


class Console:
    def __init__(self, port):
        self.s = serial.Serial(port, 115200, timeout=0.2)
        time.sleep(0.3)
        self.s.reset_input_buffer()

    def send(self, cmd):
        for ch in cmd:
            self.s.write(ch.encode())
            time.sleep(0.003)
        self.s.write(b"\r")

    _pending = b""

    def readline(self, timeout=5.0):
        """One line; reads the port in large chunks (pyserial's own readline
        fetches one byte per call, which caps a Windows COM port near 12 kB/s)."""
        end = time.time() + timeout
        while True:
            i = self._pending.find(b"\n")
            if i >= 0:
                line, self._pending = self._pending[:i + 1], self._pending[i + 1:]
                return line.decode("ascii", "replace").strip()
            if time.time() >= end:
                if self._pending:
                    line, self._pending = self._pending, b""
                    return line.decode("ascii", "replace").strip()
                return None
            self._pending += self.s.read(max(1, min(self.s.in_waiting, 65536)))

    def until_prompt(self, timeout=10.0):
        end = time.time() + timeout
        out = b""
        while time.time() < end:
            d = self.s.read(4096)
            out += d
            if out.rstrip().endswith(b">") and not d:
                break
        return out.decode("utf-8", "replace")


def dump(con, baud):
    con._pending = b""
    con.until_prompt(2.0)
    con.send("capture off")
    con.until_prompt(5.0)
    con.send(f"capture dump 0 {baud}" if baud else "capture dump")
    header = None
    for _ in range(50):
        line = con.readline(5.0)
        if line is None:
            break
        if line.startswith("#CAPDUMP"):
            header = line
            break
        if line.startswith("[capture]"):
            sys.exit("board says: " + line)
    if not header:
        sys.exit("no #CAPDUMP header (is the firmware from M3 on, and the serial monitor closed?)")
    n = int(re.search(r"records=(\d+)", header).group(1))
    print(f"board log: {n} records ({n / 6000:.1f} min)")
    if baud:
        con.s.baudrate = baud
    raw = bytearray()
    bad_lines = 0
    expected = 0
    t0 = time.time()
    last_print = 0
    end_line = None
    while True:
        line = con.readline(10.0)
        if line is None:
            break
        if line.startswith("#END"):
            end_line = line
            break
        if not line.startswith("P,"):
            continue
        try:
            _, idx, b64, crc = line.split(",")
            data = base64.b64decode(b64)
            if int(crc, 16) != crc16(data) or int(idx) != expected:
                raise ValueError
        except ValueError:
            bad_lines += 1
            break
        raw += data
        expected += len(data) // 32
        if time.time() - last_print > 1.0:
            last_print = time.time()
            print(f"\r  {expected}/{n} records ({100 * expected / max(n, 1):.0f} %)", end="", flush=True)
    if baud:
        con.s.baudrate = 115200
        con.readline(2.0)
    print(f"\r  {expected}/{n} records in {time.time() - t0:.1f} s" + " " * 10)
    if bad_lines or end_line is None or expected != n:
        return None, f"transfer error (bad lines {bad_lines}, end {'seen' if end_line else 'missing'}, " \
                     f"{expected}/{n} records)"
    return bytes(raw), None


def decode(raw):
    """-> {device_session: [record tuples]}, counters"""
    sessions = {}
    erased = badsum = 0
    for off in range(0, len(raw), 32):
        chunk = raw[off:off + 32]
        if chunk[:4] == b"\xff\xff\xff\xff":
            erased += 1
            continue
        if not check_ok(chunk):
            badsum += 1
            continue
        r = REC.unpack(chunk)
        sessions.setdefault(r[14], []).append(r)  # r[14] = session
    return sessions, erased, badsum


def read_manifest(path):
    if not os.path.exists(path):
        return []
    with open(path, newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def next_session_id(rows, raw_dir):
    used = {r["session_id"] for r in rows}
    used |= {f[:-4] for f in os.listdir(raw_dir) if re.fullmatch(r"S\d+\.csv", f)}
    k = 1
    while f"S{k:02d}" in used:
        k += 1
    return f"S{k:02d}"


def ask(prompt, given, pattern, example):
    if given:
        if not re.fullmatch(pattern, given):
            sys.exit(f"{prompt}: '{given}' does not match {example}")
        return given
    while True:
        v = input(f"  {prompt} ({example}): ").strip()
        if re.fullmatch(pattern, v):
            return v
        print(f"    please type something like {example}")


def write_session(sid, recs, raw_dir):
    path = os.path.join(raw_dir, f"{sid}.csv")
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(SCHEMA)
        for r in recs:
            t, ax, ay, az, gx, gy, gz, als, ps, temp, rh, label, flags, trial, _sess, _chk = r
            w.writerow([sid, trial, LABELS[label] if label < 4 else label, t,
                        f"{ax / 800:.4f}", f"{ay / 800:.4f}", f"{az / 800:.4f}",
                        f"{gx / 60:.3f}", f"{gy / 60:.3f}", f"{gz / 60:.3f}",
                        f"{als * 0.35:.2f}", ps, 1 if flags & 0x40 else 0,
                        f"{temp / 100:.2f}", f"{rh / 100:.2f}", flags & 0x3F])
    return path


def cmd_dump(a):
    raw_dir = os.path.join(a.data_dir, "raw")
    os.makedirs(raw_dir, exist_ok=True)
    man_path = os.path.join(a.data_dir, "manifest.csv")
    con = Console(a.port)
    raw, err = dump(con, a.baud)
    if err and a.baud:
        print(f"{err}; retrying at 115200 baud")
        raw, err = dump(con, 0)
    if err:
        sys.exit(err)
    backup = os.path.join(ROOT, "logs", f"capture_dump_{datetime.datetime.now():%Y%m%d_%H%M%S}.bin")
    os.makedirs(os.path.dirname(backup), exist_ok=True)
    with open(backup, "wb") as f:
        f.write(raw)
    sessions, erased, badsum = decode(raw)
    print(f"decoded: {sum(len(v) for v in sessions.values())} records in {len(sessions)} session(s); "
          f"skipped {erased} erased, {badsum} bad checksum. Raw backup: {os.path.relpath(backup, ROOT)}")
    rows = read_manifest(man_path)
    keys = {r.get("source_key") for r in rows}
    new_rows = []
    today = datetime.date.today().isoformat()
    for ds in sorted(sessions):
        recs = sessions[ds]
        key = f"dev{ds}:{recs[0][0]}:{len(recs)}"
        summary = {}  # label -> trials, records (r[11] = label, r[13] = trial)
        for r in recs:
            summary.setdefault(r[11], {"trials": set(), "n": 0})
            summary[r[11]]["trials"].add(r[13])
            summary[r[11]]["n"] += 1
        desc = ", ".join(f"{LABELS[l]} {len(v['trials'])} trials / {v['n'] / 100:.0f} s"
                         for l, v in sorted(summary.items()))
        if key in keys:
            print(f"board session {ds}: already imported ({desc}), skipped")
            continue
        sid = next_session_id(rows + new_rows, raw_dir)
        print(f"board session {ds} -> {sid}: {desc}")
        op = ask("operator code", a.operator, r"P\d+", "P1")
        bag = ask("bag code", a.bag, r"B\d+", "B1")
        load = ask("load", a.load, r"empty|loaded", "empty / loaded")
        loc = ask("location code", a.location, r"L\d+", "L1")
        notes = a.notes if a.notes is not None else input("  notes (optional, no names): ").strip()
        path = write_session(sid, recs, raw_dir)
        print(f"  wrote {os.path.relpath(path, a.data_dir)}")
        for l, v in sorted(summary.items()):
            new_rows.append({"session_id": sid, "date": today, "operator": op, "bag": bag, "load": load,
                             "location": loc, "label": LABELS[l], "n_trials": len(v["trials"]),
                             "seconds": f"{v['n'] / 100:.1f}", "notes": notes, "source_key": key})
    if new_rows:
        exists = os.path.exists(man_path)
        with open(man_path, "a", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=MANIFEST)
            if not exists:
                w.writeheader()
            w.writerows(new_rows)
        print(f"manifest: +{len(new_rows)} rows in {os.path.relpath(man_path, a.data_dir)}")
    print("dump OK. When the files look right, run: capture.py wipe")


def cmd_status(a):
    con = Console(a.port)
    con.until_prompt(2.0)
    con.send("capture")
    print(con.until_prompt(5.0).replace("\r", ""))


def cmd_wipe(a):
    if input("Erase every trial on the board? Only after a good dump. Type yes: ").strip() != "yes":
        sys.exit("not wiped")
    con = Console(a.port)
    con.until_prompt(2.0)
    con.send("capture off")
    con.until_prompt(5.0)
    con.send("capture wipe yes")
    print(con.until_prompt(120.0).replace("\r", ""))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("action", choices=["status", "dump", "wipe"])
    p.add_argument("--port", default="COM3")
    p.add_argument("--baud", type=int, default=921600)
    p.add_argument("--data-dir", default=os.path.join(ROOT, "data"))
    p.add_argument("--operator")
    p.add_argument("--bag")
    p.add_argument("--load")
    p.add_argument("--location")
    p.add_argument("--notes")
    a = p.parse_args()
    {"status": cmd_status, "dump": cmd_dump, "wipe": cmd_wipe}[a.action](a)


if __name__ == "__main__":
    main()
