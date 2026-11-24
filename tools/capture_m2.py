#!/usr/bin/env python3
"""Guided M2 evidence capture: sends the console commands, tells you what to do.

Run it with PlatformIO's Python (it has pyserial), from the project folder:

    & "$env:USERPROFILE\\.platformio\\penv\\Scripts\\python.exe" tools\\capture_m2.py imu
    ... tools\\capture_m2.py als
    ... tools\\capture_m2.py env
    ... tools\\capture_m2.py all        (the three, one after another)
    ... tools\\capture_m2.py leds | buzzer | keys | io   (io = those three)
    ... tools\\capture_m2.py lcd        (screen: asks what you see)
    ... tools\\capture_m2.py matrix     (LED matrix: asks what you see)
    ... tools\\capture_m2.py rtc        (RTC: set from the PC clock, rate check, RESET)
    ... tools\\capture_m2.py storage    (microSD detect + SPI flash ID and write test)
    ... tools\\capture_m2.py ir         (IR LED -> receiver loopback, optional remote)
    ... tools\\capture_m2.py codec      (ES8388 register dump + write/read-back, no audio)

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
# sends a command; cues = [(seconds after sending, text), ...] shown on screen;
# ("ask", question[, words or "text"]) records what YOU saw or heard (y/n or a few words) in the
# _cues.txt file, for things only a person can check (LED colour, sound).
# ("reset", text) asks for a RESET press and waits for the board to boot again.
# In a command, {now} is replaced by the PC's local time ("YYYY-MM-DD hh:mm:ss").
COLOURS = ["red", "green", "blue", "yellow", "black", "white"]

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
                "   The light/proximity sensor is the AP3216C (U7): with the long pin header at the top and\n"
                "   the USB ports on the right, it is the small black part on the LEFT edge, just below the\n"
                "   screen, next to the printed text 'Type 1A/3A'."),
        ("cmd", "als", 10, []),
        ("cmd", "als stream 40", 25, [
            (0.2, "hands off (room light)"),
            (3.0, "COVER the light sensor (left edge, below the screen) with your hand, about 1 cm above it"),
            (6.0, "take your hand away"),
            (7.0, "SHINE the phone torch straight at the sensor (left edge, below the screen)"),
            (9.5, "torch off"),
        ]),
        ("cmd", "i2c stats", 10, []),
    ],
    "env": [
        ("say", "Leave the board alone for now. You will breathe on the\n"
                "   temperature/humidity sensor (AHT21, U8) during the stream: the small silver square in the\n"
                "   BOTTOM-LEFT corner, under the 'PMOD1' label, just above the IR receiver."),
        ("cmd", "env", 10, []),
        ("cmd", "env stream 30", 45, [
            (0.2, "hands off"),
            (10.0, "BREATHE on the silver sensor in the bottom-left corner: mouth ~2 cm away, slow warm 'haaa' for 3 s"),
            (13.0, "stop, leave it alone"),
        ]),
        ("cmd", "i2c stats", 10, []),
    ],
    "leds": [
        ("say", "Find the indicator LED D4: hold the board with the long pin header at the top and\n"
                "   the USB ports on the right. D4 is just RIGHT of the big ST chip and is blinking red now."),
        ("ask", "Is that LED blinking RED, about once per second (0.5 s on, 0.5 s off)?"),
        ("cmd", "led red on", 5, []),
        ("ask", "Is it now steady RED (not blinking)?"),
        ("cmd", "led red off", 5, []),
        ("cmd", "led blue on", 5, []),
        ("ask", "Is a steady BLUE light now on at the same spot (D4)? If blue is somewhere else, say where."),
        ("cmd", "led red on", 5, []),
        ("ask", "Red and blue together: do you see both on (a purple/pink mix)?"),
        ("cmd", "led red off", 5, []),
        ("cmd", "led blue off", 5, []),
        ("ask", "Is D4 completely off now?"),
        ("cmd", "led auto", 5, []),
        ("ask", "Is D4 blinking red again, once per second?"),
    ],
    "buzzer": [
        ("say", "The buzzer is the round black part with a '+' on it, near the bottom middle of the board.\n"
                "   Listen for each sound, then answer the question."),
        ("ask", "Was the buzzer SILENT when you pressed RESET and while the board was idle?"),
        ("cmd", "buzzer beep 300", 5, []),
        ("ask", "Did you hear ONE beep (about a third of a second)?"),
        ("cmd", "buzzer chirp", 5, []),
        ("ask", "Did you hear one very short chirp?"),
        ("cmd", "buzzer error", 5, []),
        ("ask", "Did you hear THREE short beeps?"),
        ("cmd", "buzzer alarm", 8, []),
        ("ask", "Did you hear beep-beep-beep for about 3 seconds, and did it STOP by itself?"),
    ],
    "keys": [
        ("say", "The arrow keys UP, DOWN, LEFT, RIGHT are at the bottom right (RESET is the red one: do NOT\n"
                "   press it). Press a key only when told. Keep your hands off the PC keyboard while it listens."),
        # One short listen per key, so every event in the log belongs to exactly
        # one instruction (checks that each printed label maps to its pin).
        ("say", "Each step listens for 4 s. Press ONLY the key named on the big line, ONCE, then let go."),
        ("cmd", "keys 4", 10, [(0.3, "press UP once (the single key above the row), then let go")]),
        ("cmd", "keys 4", 10, [(0.3, "press DOWN once (bottom row, middle), then let go")]),
        ("cmd", "keys 4", 10, [(0.3, "press LEFT once (bottom row, next to the red RESET), then let go")]),
        ("cmd", "keys 4", 10, [(0.3, "press RIGHT once (bottom row, right end), then let go")]),
        ("say", "Next: a LONG press. Hold UP down until the screen says LET GO."),
        ("cmd", "keys 5", 10, [(0.3, "press and HOLD UP ..."), (2.8, "LET GO of UP")]),
        ("say", "Last: quick taps."),
        ("cmd", "keys 4", 10, [(0.3, "tap RIGHT 3 times quickly, then hands off")]),
    ],
    "lcd": [
        ("say", "Hold the board with the long pin header at the TOP and the USB ports on the RIGHT, and keep it\n"
                "   that way: 'top-left' etc. below mean the screen as you see it in this position.\n"
                "   The screen should be dark now (backlight off until the first lcd command)."),
        ("cmd", "lcd", 10, []),
        ("ask", "Is the screen now lit (backlight on) and evenly BLACK, with no noise or stripes?"),
        ("cmd", "lcd bus", 10, []),
        ("cmd", "lcd te", 10, []),
        ("cmd", "lcd fill white", 5, []),
        ("ask", "Is the WHOLE screen white and evenly lit, with no dead lines or dark bands?"),
        ("cmd", "lcd fill red", 5, []),
        ("ask", "Is the whole screen RED (not blue)?"),
        ("cmd", "lcd fill green", 5, []),
        ("ask", "Is the whole screen GREEN?"),
        ("cmd", "lcd fill blue", 5, []),
        ("ask", "Is the whole screen BLUE (not red)?"),
        ("cmd", "lcd bl off", 5, []),
        ("ask", "Backlight off: did the screen go dark (blue image almost invisible)?"),
        ("cmd", "lcd bl on", 5, []),
        ("cmd", "lcd test", 5, []),
        ("ask", "Test pattern: is a thin WHITE frame visible along ALL FOUR edges? If a side is missing, say which."),
        ("ask", "Which colour is the square in the TOP-LEFT corner?", COLOURS),
        ("ask", "Which colour is the square in the TOP-RIGHT corner?", COLOURS),
        ("ask", "Which colour is the square in the BOTTOM-LEFT corner?", COLOURS),
        ("ask", "Which colour is the square in the BOTTOM-RIGHT corner?", COLOURS),
        ("ask", "The three bars in the middle, from LEFT to RIGHT: which colours? (three words)", COLOURS),
        ("ask", "Photo: take one of the test pattern (whole board, header at the top), save it as "
                "evidence/m2_lcd_test.jpg, then type 'done' (or 'skip').", ["done", "skip"]),
        ("cmd", "lcd", 10, []),
    ],
    "matrix": [
        ("say", "Hold the board with the long pin header at the TOP and the USB ports on the RIGHT.\n"
                "   Look at the LED matrix: 19 small RGB LEDs under the white diffuser. They are dim on purpose."),
        ("ask", "Before the first matrix command: are ALL matrix LEDs off (dark)?"),
        ("cmd", "matrix", 10, []),
        ("ask", "After 'matrix' (init): still ALL off?"),
        ("cmd", "matrix fill 16 0 0", 5, []),
        ("ask", "Are ALL the matrix LEDs lit RED (not green, none dark)? If some are dark or another colour, say how many."),
        ("cmd", "matrix fill 0 16 0", 5, []),
        ("ask", "Are ALL of them GREEN now (not red)?"),
        ("cmd", "matrix fill 0 0 16", 5, []),
        ("ask", "Are ALL of them BLUE now?"),
        ("cmd", "matrix fill 16 16 16", 5, []),
        ("ask", "Are ALL of them WHITE-ish now (no LED clearly red/green/blue only)?"),
        ("cmd", "matrix off", 5, []),
        ("ask", "Are ALL of them off again?"),
        ("say", "Next: one LED at a time walks through the chain (0.4 s each, about 8 s). Count the positions."),
        ("cmd", "matrix chase 400", 20, [(0.3, "watch the matrix and COUNT how many different LEDs light up")]),
        ("ask", "How many different LEDs lit up, one at a time? (type the number you counted)",
         [str(n) for n in range(0, 41)]),
        ("ask", "Where did it start, and how did it move? (e.g. 'centre, then the ring clockwise')", "text"),
        ("cmd", "matrix", 10, []),
    ],
    "ir": [
        ("say", "IR test. The IR receiver is in the BOTTOM-LEFT corner (header at the top), below the silver\n"
                "   AHT21 sensor. Keep the board flat and do not cover that corner. About 30 s."),
        ("cmd", "ir", 10, []),
        ("cmd", "ir loop", 10, []),
        ("cmd", "ir loop dry", 10, []),
        ("cmd", "ir loop 64", 10, []),
        ("cmd", "ir loop dry", 10, []),
        ("say", "Optional: if you have an IR remote (TV, aircon, ...), have it ready. In the next 10 s, point it\n"
                "   at the bottom-left corner and press a button 2-3 times. No remote: just wait."),
        ("cmd", "ir rx 10", 20, [(0.3, "remote: press a button 2-3 times now, pointed at the bottom-left corner")]),
        ("ask", "Did you press a remote during that listen? (type: remote / none)", ["remote", "none"]),
    ],
    "codec": [
        ("say", "Audio codec test (control registers only, no sound, no recording). No hands needed after\n"
                "   the RESET. About 10 s."),
        ("cmd", "codec", 10, []),
        ("cmd", "codec test", 10, []),
        ("cmd", "codec", 10, []),
        ("cmd", "i2c stats", 10, []),
    ],
    "storage": [
        ("say", "Storage test: no hands needed after the RESET. About 15 s. The SD slot stays EMPTY."),
        ("cmd", "sd", 15, []),
        ("cmd", "sd", 15, []),
        ("cmd", "flash", 10, []),
        ("cmd", "flash test", 15, []),
        ("cmd", "flash test", 15, []),
        ("cmd", "diag mem", 10, []),
    ],
    "rtc": [
        ("say", "RTC test: no hands needed except one RESET press. It takes about 1.5 minutes."),
        ("cmd", "rtc", 15, []),
        ("cmd", "rtc set {now}", 10, []),
        ("cmd", "rtc", 10, []),
        ("cmd", "rtc check 20", 35, []),
        ("cmd", "rtc check 20", 35, []),
        ("reset", "The RTC should keep running through a RESET (same power)."),
        ("cmd", "rtc", 15, []),
        ("cmd", "rtc set 2026-02-30 10:00:00", 10, []),
        ("cmd", "rtc", 10, []),
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
                if step[0] == "ask":
                    board.poll()
                    print("\n?? " + step[1])
                    words = step[2] if len(step) > 2 else None
                    ans = ""
                    while not ans:
                        if words == "text":
                            ans = input("?? Describe it in a few words (not y/n), then Enter: ").strip()
                            if ans.lower() in ("y", "n", "yes", "no"):
                                print("   Please describe what you saw, in words.")
                                ans = ""
                        elif words:
                            ans = input("?? Type one of: %s, then Enter: " % " / ".join(words)).strip().lower()
                            given = ans.replace(",", " ").split()
                            if not given or any(w not in words for w in given):
                                print("   Please answer with those words only (not y/n).")
                                ans = ""
                            elif ans == "done":
                                photo = os.path.join(out_dir, "m2_%s_test.jpg" % name)
                                if not os.path.exists(photo):
                                    print("   %s is not there yet. Save it, then type done (or skip)." % photo)
                                    ans = ""
                        else:
                            ans = input("?? Answer y / n (or a few words), then Enter: ").strip()
                    cues.write("%s answer: %s -> %s\n" % (ts(), step[1], ans))
                    cues.flush()
                    continue
                if step[0] == "say":
                    print("\n>> " + step[1])
                    input(">> Press Enter when ready... ")
                    board.poll()
                    continue
                if step[0] == "reset":
                    print("\n>> " + step[1])
                    print(">> Press the RESET button on the board now.")
                    cues.write("%s cue: press RESET (%s)\n" % (ts(), step[1]))
                    n_ready = sum("[cli] ready" in l for l in board.lines)
                    if not board.wait_for(lambda: sum("[cli] ready" in l for l in board.lines) > n_ready, 60):
                        print("!! no reboot seen within 60 s")
                    board.wait_for(board.at_prompt, 3)
                    cues.flush()
                    continue
                _, cmd, timeout, cue_list = step
                # {now} = the PC's local time when the command is sent.
                cmd = cmd.replace("{now}", datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"))
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
        ser.write(b"x")  # stop the stream...
        time.sleep(0.3)
        ser.write(b"\x08")  # ...and erase the key from the command line
        print("\nstopped.")
    finally:
        ser.close()
    return 0


CHECK_ALS_CUES = [
    (0.0, "leave the board alone (room light)"),
    (5.0, "COVER THE WHOLE BOARD (book, cup or both hands) so no light reaches it"),
    (10.0, "uncover, then hold the phone torch ~10 cm above the board, shining down on all of it"),
    (15.0, "torch off, hands away"),
]


def check_als(port):
    """20 s timed light test for diagnosing the AP3216C: room light, whole board
    covered, torch over the whole board. Readout and cues go to
    logs/als_check_<date-time>.log (git-ignored scratch, not evidence)."""
    try:
        ser = serial.Serial(port, BAUD, timeout=0.05)
    except serial.SerialException as e:
        print("Cannot open %s: %s\nClose any other serial monitor and try again." % (port, e))
        return 1
    os.makedirs(os.path.join(ROOT, "logs"), exist_ok=True)
    path = os.path.join(ROOT, "logs", "als_check_%s.log" % datetime.datetime.now().strftime("%y%m%d-%H%M%S"))
    out = open(path, "w", encoding="utf-8", newline="\n")
    print("\nLIGHT SENSOR CHECK (20 s). Have the phone torch ready.")
    input(">> Press Enter to start...")
    ser.write(b"x")  # stop anything still streaming
    time.sleep(0.3)
    ser.write(b"\x08")
    time.sleep(0.05)
    ser.reset_input_buffer()
    for ch in "als stream 200 100":
        ser.write(ch.encode())
        time.sleep(0.004)
    ser.write(b"\r")
    t0 = time.time()
    cues = list(CHECK_ALS_CUES)
    buf = ""
    try:
        while True:
            el = time.time() - t0
            while cues and el >= cues[0][0]:
                msg = cues.pop(0)[1]
                print("\n   ##########  NOW: %s  ##########\n" % msg)
                out.write("%s # cue: %s\n" % (ts(), msg))
            buf += ser.read(4096).decode("utf-8", "replace")
            while "\n" in buf:
                line, buf = buf.split("\n", 1)
                line = line.strip()
                if not line:
                    continue
                out.write("%s > %s\n" % (ts(), line))
                parts = line.split(",")
                if len(parts) == 8 and parts[0] == "als":
                    print("  lux %8.2f   PS %5s   IR %5s" % (float(parts[2]), parts[3], parts[4]))
                elif line.startswith("# als stream end"):
                    print("\n" + line)
                    print("Done. Saved to %s. Tell the agent." % os.path.relpath(path, ROOT))
                    return 0
            if el > 30.0:
                print("No end of stream seen after 30 s; stopping. Saved to %s." % os.path.relpath(path, ROOT))
                return 1
    except KeyboardInterrupt:
        ser.write(b"x")
        time.sleep(0.3)
        ser.write(b"\x08")
        print("\nstopped. Partial log in %s." % os.path.relpath(path, ROOT))
        return 1
    finally:
        out.close()
        ser.close()


def main():
    ap = argparse.ArgumentParser(description="Guided M2 sensor evidence capture.")
    ap.add_argument("sensor", choices=["imu", "als", "env", "all", "leds", "buzzer", "keys", "io", "lcd", "matrix", "rtc", "storage", "ir", "codec",
                                       "find-als", "check-als"],
                    help="all = imu, als, env; io = leds, buzzer, keys; find-als: live light/proximity readout to locate the sensor (writes no files); "
                         "check-als: 20 s timed cover/torch test, saved to logs/ (not evidence)")
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
    if args.sensor == "check-als":
        return check_als(port)

    groups = {"all": ["imu", "als", "env"], "io": ["leds", "buzzer", "keys"]}
    names = groups.get(args.sensor, [args.sensor])
    ok = all(run_plan(n, port, args.force, args.out_dir) for n in names)
    if ok:
        print("\nAll done. Tell the agent the evidence is in.")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
