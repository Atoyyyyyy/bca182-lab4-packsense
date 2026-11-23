#!/usr/bin/env python3
"""Guided M2 evidence capture: sends the console commands, tells you what to do.

Run it with PlatformIO's Python (it has pyserial), from the project folder:

    & "$env:USERPROFILE\\.platformio\\penv\\Scripts\\python.exe" tools\\capture_m2.py imu
    ... tools\\capture_m2.py als
    ... tools\\capture_m2.py env
    ... tools\\capture_m2.py all        (the three, one after another)

Options: --port COM3 (default: auto-detect the ST-LINK virtual COM port),
         --flash (build + upload first), --force (overwrite an existing log),
         --out-dir DIR (practice run: write somewhere other than evidence/).

What it does, per sensor:
  1. opens the serial port and asks YOU to press RESET on the board;
  2. waits for the firmware's "[cli] ready" line;
  3. types the commands of docs/test_sheets/m2_sensors.md one by one, waiting
     for each to finish, and shows on screen WHEN to tilt / cover / breathe;
  4. writes everything the board sends to evidence/m2_<sensor>.log, with the
     same "HH:MM:SS.mmm > " timestamps as `pio device monitor` (time filter).

The log contains only bytes received from the board. The on-screen cues (what
you were asked to do, and when) go to evidence/m2_<sensor>_cues.txt, so the
analysis can line them up with the data. Close any other serial monitor first:
only one program can open the port.
"""

import argparse
import datetime
import os
import subprocess
import sys
import time

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    sys.exit("pyserial is missing. Run this with PlatformIO's Python:\n"
             "  & \"$env:USERPROFILE\\.platformio\\penv\\Scripts\\python.exe\" tools\\capture_m2.py imu")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BAUD = 115200

# Each step: ("say", text) waits for Enter; ("cmd", command, timeout_s, cues)
# sends a command; cues = [(seconds after sending, text), ...] shown on screen.
PLANS = {
    "imu": [
        ("say", "Lay the board FLAT on the table, components up. Do not touch it until told."),
        ("cmd", "i2c scan", 10, []),
        ("cmd", "imu", 10, []),
        ("cmd", "imu cal", 15, [(0.2, "keep the board still (calibrating gyro, 2 s)")]),
        ("cmd", "imu stream 1000", 25, [(0.2, "keep the board still (10 s)")]),
        ("say", "Next: a 5 s stream while you TILT the board. Get ready to pick it up."),
        ("cmd", "imu stream 500", 20, [
            (0.3, "PICK UP the board and tilt it onto its LEFT edge"),
            (1.5, "tilt onto its RIGHT edge"),
            (2.5, "tilt onto its FRONT edge"),
            (3.5, "tilt onto its BACK edge"),
            (4.5, "put it back down FLAT"),
        ]),
        ("cmd", "diag mem", 10, []),
    ],
    "als": [
        ("say", "Leave the board in normal room light. Have a phone torch ready.\n"
                "   The light/proximity sensor is the AP3216C (U7), near the TOP-RIGHT corner of the board."),
        ("cmd", "als", 10, []),
        ("cmd", "als stream 40", 25, [
            (0.2, "hands off (room light)"),
            (3.0, "COVER the light sensor (top-right corner) with your hand, about 1 cm above it"),
            (6.0, "take your hand away"),
            (7.0, "SHINE the phone torch straight at the sensor (top-right corner)"),
            (9.5, "torch off"),
        ]),
        ("cmd", "i2c stats", 10, []),
    ],
    "env": [
        ("say", "Leave the board alone for now. You will breathe on the\n"
                "   temperature/humidity sensor (AHT, U8 on the schematic) during the stream."),
        ("cmd", "env", 10, []),
        ("cmd", "env stream 30", 45, [
            (0.2, "hands off"),
            (10.0, "BREATHE gently on the temperature/humidity sensor, once or twice"),
            (13.0, "stop, leave it alone"),
        ]),
        ("cmd", "i2c stats", 10, []),
    ],
}


def ts():
    return datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]


def find_port():
    for p in serial.tools.list_ports.comports():
        if (p.vid, p.pid) == (0x0483, 0x374B) or "stlink" in (p.description or "").lower():
            return p.device
    return None


class Board:
    """Reads lines from the board, timestamps them, writes them to the log."""

    def __init__(self, port, log_fp):
        self.ser = serial.Serial(port, BAUD, timeout=0.05)
        self.log = log_fp
        self.partial = ""
        self.partial_t = None
        self.last_rx = time.time()
        self.lines = []

    def close(self):
        self.ser.close()

    def poll(self):
        data = self.ser.read(4096)
        if not data:
            return
        self.last_rx = time.time()
        for ch in data.decode("utf-8", "replace"):
            if self.partial_t is None:
                self.partial_t = ts()
            if ch == "\n":
                line = self.partial.rstrip("\r")
                self.log.write("%s > %s\n" % (self.partial_t, line))
                self.log.flush()
                print("   | " + line)
                self.lines.append(line)
                self.partial = ""
                self.partial_t = None
            else:
                self.partial += ch

    def send(self, text):
        for ch in text:
            self.ser.write(ch.encode())
            time.sleep(0.004)
        self.ser.write(b"\r")

    def wait_for(self, pred, timeout):
        end = time.time() + timeout
        while time.time() < end:
            self.poll()
            if pred():
                return True
        return False

    def at_prompt(self):
        """Command finished: the CLI printed its '> ' prompt and went quiet."""
        return self.partial.rstrip("\r").endswith("> ") and time.time() - self.last_rx > 0.4


def run_plan(name, port, force, out_dir):
    out = os.path.join(out_dir, "m2_%s.log" % name)
    cues_path = os.path.join(out_dir, "m2_%s_cues.txt" % name)
    if os.path.exists(out) and not force:
        print("%s already exists. Use --force to overwrite it." % out)
        return False

    print("\n=== %s: capturing to %s" % (name.upper(), out))
    with open(out, "w", encoding="utf-8", newline="\n") as log, \
            open(cues_path, "w", encoding="utf-8", newline="\n") as cues:
        cues.write("# operator cues shown by tools/capture_m2.py (PC time, %s)\n" % datetime.date.today())
        try:
            board = Board(port, log)
        except serial.SerialException as e:
            print("Cannot open %s: %s\nClose any other serial monitor and try again." % (port, e))
            return False
        try:
            print(">> Press the RESET button on the board now.")
            if not board.wait_for(lambda: any("[cli] ready" in l for l in board.lines), 60):
                print("No '[cli] ready' line within 60 s. Is the board flashed with the M2 firmware?")
                return False
            board.wait_for(board.at_prompt, 3)
            banner = next((l for l in board.lines if l.startswith("=== PackSense")), "")
            if "-dirty" in banner:
                print("WARNING: firmware was built from uncommitted changes (%s)." % banner)
                print("         For evidence, rebuild from a clean tree (--flash after committing).")

            for step in PLANS[name]:
                if step[0] == "say":
                    print("\n>> " + step[1])
                    input(">> Press Enter when ready... ")
                    board.poll()
                    continue
                _, cmd, timeout, cue_list = step
                print("\n>> sending: %s" % cmd)
                cues.write("%s sent: %s\n" % (ts(), cmd))
                board.send(cmd)
                t0 = time.time()
                pending = list(cue_list)
                start_lines = len(board.lines)
                while time.time() - t0 < timeout:
                    board.poll()
                    while pending and time.time() - t0 >= pending[0][0]:
                        text = pending.pop(0)[1]
                        print("\n   ##########  NOW: %s  ##########\n" % text)
                        cues.write("%s cue: %s\n" % (ts(), text))
                    if not pending and len(board.lines) > start_lines and board.at_prompt():
                        break
                else:
                    print("!! '%s' did not finish within %d s" % (cmd, timeout))
                cues.flush()
            board.wait_for(lambda: False, 0.5)
        finally:
            board.close()
    print("\n=== %s done: %s and %s" % (name.upper(), out,
                                       cues_path))
    return True


def find_als(port):
    """Live readout to locate the AP3216C: move a fingertip over the board until
    PS jumps. Nothing is written to disk. Ctrl+C to stop."""
    try:
        ser = serial.Serial(port, BAUD, timeout=0.05)
    except serial.SerialException as e:
        print("Cannot open %s: %s\nClose any other serial monitor and try again." % (port, e))
        return 1
    print("\nLIVE LIGHT/PROXIMITY FINDER (nothing is saved). Ctrl+C to stop.")
    print("Hold a fingertip ~1 cm above one small chip at a time, 2-3 s each.")
    print("Over the right chip, PS jumps into the hundreds and the bar fills.\n")
    buf = ""
    last_send = 0.0
    try:
        while True:
            now = time.time()
            if now - last_send > 31.0:  # restart the stream before it ends (300 x 100 ms = 30 s)
                ser.write(b"x")  # any key stops a running stream; else it is erased below
                time.sleep(0.3)
                ser.write(b"\x08")  # if no stream was running, the 'x' sits in the line: erase it
                time.sleep(0.05)
                ser.reset_input_buffer()
                for ch in "als stream 300 100":
                    ser.write(ch.encode())
                    time.sleep(0.004)
                ser.write(b"\r")
                last_send = time.time()
            buf += ser.read(4096).decode("utf-8", "replace")
            while "\n" in buf:
                line, buf = buf.split("\n", 1)
                parts = line.strip().split(",")
                if len(parts) == 8 and parts[0] == "als":
                    try:
                        lux, ps = float(parts[2]), int(parts[3])
                    except ValueError:
                        continue
                    bar = "#" * min(40, ps // 25)
                    flag = "   <<< SENSOR IS HERE" if ps >= 200 else ""
                    print("  lux %8.1f   PS %5d  |%-40s|%s" % (lux, ps, bar, flag))
                elif "init" in line or "error" in line.lower():
                    print("  " + line.strip())
    except KeyboardInterrupt:
        ser.write(b"x")
        print("\nstopped.")
    finally:
        ser.close()
    return 0


def main():
    ap = argparse.ArgumentParser(description="Guided M2 sensor evidence capture.")
    ap.add_argument("sensor", choices=["imu", "als", "env", "all", "find-als"],
                    help="find-als: live light/proximity readout to locate the sensor (writes no files)")
    ap.add_argument("--port", help="serial port (default: auto-detect the ST-LINK VCP)")
    ap.add_argument("--flash", action="store_true", help="build and upload the firmware first")
    ap.add_argument("--force", action="store_true", help="overwrite existing evidence logs")
    ap.add_argument("--out-dir", default=os.path.join(ROOT, "evidence"),
                    help="where to write the logs (default: evidence/; use another folder for a practice run)")
    args = ap.parse_args()

    if args.flash:
        pio = os.path.join(os.path.expanduser("~"), ".platformio", "penv", "Scripts", "pio.exe")
        print("Building and flashing...")
        if subprocess.call([pio, "run", "-t", "upload"], cwd=ROOT) != 0:
            sys.exit("Upload failed.")

    port = args.port or find_port()
    if not port:
        sys.exit("ST-LINK virtual COM port not found. Plug the board into the ST-LINK USB port, "
                 "or pass --port COMx.")
    print("Using %s" % port)

    if args.sensor == "find-als":
        return find_als(port)

    names = ["imu", "als", "env"] if args.sensor == "all" else [args.sensor]
    ok = all(run_plan(n, port, args.force, args.out_dir) for n in names)
    if ok:
        print("\nAll done. Tell the agent the evidence is in.")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
