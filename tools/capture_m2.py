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
    ... tools\\capture_m2.py wifi       (RW007 ID/firmware/MAC, 2 scans, flash on the shared bus)
    ... tools\\capture_m2.py m5_parity  (M5: selftest model + 30 s of live windows -> evidence/m5_parity.log)
    ... tools\\capture_m2.py m5_func    (M5: guided per-class check, GUARD alarm, fault injection)
    ... tools\\capture_m2.py m6_t1_still | m6_t1_carried | m6_t1_rough | m6_t1_access
                                         (M6 T1: 20 new trials per class from docs/test_sheets/T1.csv)
    ... tools\\capture_m2.py m6_soak    (M6 T4: 15+ min of mixed use, Wi-Fi module on)
    ... tools\\capture_m2.py m6_offline (M6 T5: 5+ min of mixed use, Wi-Fi module held off)
    ... tools\\capture_m2.py m6_eventlog (M6 T5 part B: read the event log after a power-bank run)
    ... tools\\capture_m2.py m6_fault   (M6 T6: sensor faults, SD absent, Wi-Fi loss, RESET, watchdog)
    ... tools\\capture_m2.py m6_keys    (M6 T7: acknowledge, arm/disarm, mute, mode switch, 10x each)
    ... tools\\capture_m2.py m6_diag    (M6 T8: walk the DIAG pages + every module's console check)
    The M6 plans also write <name>_notes.csv (start,end,expected per timed step) for
    tools/t1_results.py, tools/t7_keys.py and tools/analyze_soak.py --notes.

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
import codecs
import datetime
import os
import subprocess
import sys
import threading
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
# ("reboot", text, timeout_s) waits for a reboot the board does by itself (watchdog).
# ("seg", secs, expected, text[, trial_id]) is a timed step like "wait" whose start
# and end go to <name>_notes.csv with the expected label (state or key action), for
# the M6 analysis scripts. With a trial_id the operator then confirms the trial
# (Enter = done as described, x = went wrong); the answer goes in the "ok" column.
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
    "wifi": [
        ("say", "Wi-Fi test: RW007 module ID, firmware, MAC and two scans (no network is joined; network\n"
                "   names are not logged, only their length). Then the SPI flash test on the same bus.\n"
                "   No hands needed after the RESET. About 20 s."),
        ("cmd", "wifi", 15, []),
        ("cmd", "wifi scan", 25, []),
        ("cmd", "wifi scan", 25, []),
        ("cmd", "flash test", 15, []),
        ("cmd", "wifi stats", 10, []),
        ("cmd", "diag mem", 10, []),
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
    # ---- M5 (product firmware; needs the trained model exported and flashed) ----
    "m5_parity": [
        ("say", "Model parity and timing (AC-3, AC-4): the board recomputes 50 test windows and compares\n"
                "   them with the PC model, then classifies live for 30 s. Leave the board still on the desk.\n"
                "   No hands needed after the RESET. About 1 minute."),
        ("cmd", "selftest model", 40, []),
        ("cmd", "selftest model v", 40, []),
        ("cmd", "infer log on", 5, []),
        ("wait", 30, "leave the board still (live windows are being logged)"),
        ("cmd", "infer log off", 5, []),
        ("cmd", "infer", 10, []),
        ("cmd", "ui", 10, []),
        ("cmd", "diag mem", 10, []),
        ("cmd", "version", 10, []),
    ],
    "m5_func": [
        ("say", "Quick functional check (M5). Put the board in the bag as for the recordings\n"
                "   (docs/test_sheets/m3_capture.md), power bank in the bag, USB cable to the laptop long enough\n"
                "   to walk a few steps (or carry the laptop). Use the DUMMY load for ROUGH. About 4 minutes."),
        ("cmd", "infer log on", 5, []),
        ("say", "STILL: put the bag down on the floor or a chair and do not touch it."),
        ("wait", 20, "STILL - do not touch the bag"),
        ("ask", "What state did the LCD show at the end?", ["still", "carried", "rough", "access", "other"]),
        ("say", "CARRIED: wear the bag on your back and walk normally."),
        ("wait", 25, "CARRIED - walk normally with the bag on your back"),
        ("ask", "What state did the LCD show while you walked?", ["still", "carried", "rough", "access", "other"]),
        ("say", "ROUGH (dummy load only!): set the bag down hard / toss it onto a cushion, about 3 times."),
        ("wait", 20, "ROUGH - set down hard / toss onto a cushion, 3 times"),
        ("ask", "Did you see 'Rough handling detected' and hear a chirp? (y/n, or what you saw)"),
        ("say", "ACCESS (RUN mode, no alarm expected): open the compartment, reach in, move items, close."),
        ("wait", 20, "ACCESS - open, reach in, move items, close"),
        ("ask", "What state did the LCD show while your hand was in the bag?",
         ["still", "carried", "rough", "access", "other"]),
        ("say", "GUARD: hold LEFT for about 1 s (header turns red, says GUARD). Then leave the bag closed."),
        ("wait", 10, "hold LEFT ~1 s to arm GUARD, then leave the bag closed"),
        ("ask", "Does the header say GUARD? (y/n)"),
        ("say", "Now open the bag and reach in. When the alarm sounds (red screen), press ANY key once."),
        ("wait", 30, "open the bag, reach in; press any key when the alarm sounds"),
        ("ask", "Did the alarm sound, and did one key press stop it? (y/n, or what happened)"),
        ("say", "Disarm: hold LEFT for about 1 s (header back to RUN)."),
        ("wait", 8, "hold LEFT ~1 s to disarm"),
        ("cmd", "fault inject imu", 5, []),
        ("wait", 8, "watch the LCD bottom line"),
        ("ask", "Does the LCD show 'Sensor fault - IMU'? (y/n)"),
        ("cmd", "fault clear imu", 5, []),
        ("wait", 10, "watch the LCD bottom line"),
        ("ask", "Did the fault line go away? (y/n)"),
        ("cmd", "infer log off", 5, []),
        ("cmd", "infer", 10, []),
        ("cmd", "ui", 10, []),
        ("cmd", "fault", 10, []),
        ("cmd", "log 40", 15, []),
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

# ---- M6 (system tests, PRD 9; procedures in docs/test_plan.md) --------------------

T1_SHEET = os.path.join(ROOT, "docs", "test_sheets", "T1.csv")
T1_CLASSES = ("still", "carried", "rough", "access")
T1_INTRO = {
    "still": "T1 STILL: 20 trials. Each time you move the bag to the place on the line, close it, press Enter\n"
             "   and step back: nobody touches the bag or the table until STOP (12 s).",
    "carried": "T1 CARRIED: 20 trials of walking. Put the bag on as the line says, stand still, press Enter,\n"
               "   then walk normally until STOP (20 s). A team mate carries the laptop next to you, or use a long\n"
               "   USB cable. Walk on level ground only (no stairs while tethered).",
    "rough": "T1 ROUGH: 20 trials. DUMMY LOAD ONLY (books, a water bottle), never a laptop, no real drops.\n"
             "   Hold the bag as the line says, press Enter, do the action until STOP (8 s), then hold the bag still.",
    "access": "T1 ACCESS: 20 trials. Bag closed on the surface on the line. Press Enter, then open the\n"
              "   compartment, reach in, move items and close it again before STOP (12 s).",
}


def t1_plan(cls):
    """One T1 run (20 trials of one class) from docs/test_sheets/T1.csv."""
    import csv
    with open(T1_SHEET, encoding="utf-8", newline="") as f:
        rows = [r for r in csv.DictReader(f) if r["class"].strip().lower() == cls]
    steps = [
        ("say", T1_INTRO[cls] + "\n   Bag set-up: board flat against the back panel, LCD facing into the compartment, as in\n"
                "   the recordings (docs/test_sheets/m3_capture.md). The USB cable leaves through the corner of\n"
                "   the zipper; close the zipper up to the cable."),
        ("cmd", "infer", 10, []),
        ("cmd", "infer log on", 5, []),
    ]
    for k, r in enumerate(rows, 1):
        where = ", ".join(x for x in (r["bag"], r["load"], r["carrier"], r["place"], r["light"]) if x)
        steps.append(("say", "%s (%d/%d) set-up: %s [%s]" % (r["trial"], k, len(rows), r["setup"], where)))
        steps.append(("seg", int(r["secs"]), r["class"].upper(), "%s: %s" % (r["trial"], r["do"]), r["trial"]))
    steps += [
        ("cmd", "infer log off", 5, []),
        ("cmd", "infer", 10, []),
        ("cmd", "ui", 10, []),
        ("cmd", "log 40", 15, []),
        ("cmd", "diag mem", 10, []),
        ("cmd", "version", 10, []),
    ]
    return steps


if os.path.exists(T1_SHEET):
    for _c in T1_CLASSES:
        PLANS["m6_t1_" + _c] = t1_plan(_c)


def _keys_plan():
    steps = [
        ("say", "T7 input control: board flat on the desk, LCD visible. Every step names ONE key action:\n"
                "   do it once, then wait for the next line. 'hold' = keep the key down about 1 s, then let go.\n"
                "   About 5 minutes."),
        ("cmd", "ui", 10, []),
    ]
    for i in range(1, 11):
        steps.append(("cmd", "alarm test", 5, []))
        steps.append(("seg", 4, "ack", "ACK %d/10: the alarm is on - press ANY key once" % i))
    for i in range(1, 11):
        steps.append(("seg", 4, "arm", "ARM %d/10: hold LEFT ~1 s, let go (header turns red: GUARD)" % i))
        steps.append(("seg", 4, "disarm", "DISARM %d/10: hold LEFT ~1 s, let go (header back to RUN)" % i))
    for i in range(1, 11):
        steps.append(("seg", 4, "mute", "MUTE %d/10: hold DOWN ~1 s, let go (MUTE in the header)" % i))
        steps.append(("seg", 4, "unmute", "UNMUTE %d/10: hold DOWN ~1 s, let go (chirp, MUTE gone)" % i))
    for i in range(1, 11):
        steps.append(("seg", 3, "diag_enter", "MODE %d/10: press UP once (DIAG page 1)" % i))
        steps.append(("seg", 3, "diag_exit", "MODE %d/10: press UP once (back to RUN)" % i))
    steps += [
        ("cmd", "ui", 10, []),
        ("cmd", "mode", 5, []),
        ("cmd", "log 20", 15, []),
    ]
    return steps


PLANS.update({
    # T4: >= 15 min of mixed use. The RW007 stays powered (online condition) so T5,
    # with it held in reset, has something to compare with.
    "m6_soak": [
        ("say", "T4 soak: 16 minutes of mixed use, following the timed lines on this screen (no Enter\n"
                "   presses until the end). Bag with the DUMMY load (there are rough-handling steps), board in\n"
                "   the bag as for the recordings, laptop next to the bag. For the walking steps a team mate\n"
                "   carries the laptop, or use a long USB cable."),
        ("cmd", "wifi on", 15, []),
        ("cmd", "infer log on", 5, []),
        ("cmd", "diag mem", 10, []),
        ("seg", 150, "STILL", "bag closed on the desk or floor, hands off"),
        ("seg", 10, "CARRIED", "pick the bag up and put it on your back"),
        ("seg", 120, "CARRIED", "walk normally with the bag on your back"),
        ("seg", 10, "STILL", "put the bag down gently, hands off"),
        ("seg", 60, "STILL", "hands off"),
        ("seg", 15, "ROUGH", "dummy load: set the bag down hard / swing it"),
        ("seg", 45, "STILL", "put it down, hands off"),
        ("seg", 15, "ACCESS", "open the bag, reach in, rummage, close"),
        ("seg", 45, "STILL", "hands off"),
        ("seg", 5, "KEYS", "hold LEFT ~1 s: arm GUARD"),
        ("seg", 30, "STILL", "GUARD armed, hands off"),
        ("seg", 15, "ACCESS", "open the bag, reach in: the alarm should sound - press any key once to stop it, close"),
        ("seg", 5, "KEYS", "hold LEFT ~1 s: disarm (back to RUN)"),
        ("seg", 60, "STILL", "hands off"),
        ("cmd", "diag mem", 10, []),
        ("seg", 15, "ROUGH", "dummy load: toss the bag onto a cushion/bed (<= 50 cm), pick it up, again"),
        ("seg", 45, "STILL", "put it down, hands off"),
        ("seg", 20, "KEYS", "press UP (DIAG), RIGHT 4 times (pages), UP (back to RUN)"),
        ("seg", 10, "KEYS", "hold DOWN ~1 s (mute), then hold DOWN ~1 s again (unmute)"),
        ("seg", 10, "CARRIED", "pick the bag up and put it on your back"),
        ("seg", 60, "CARRIED", "walk normally"),
        ("seg", 10, "STILL", "put the bag down gently, hands off"),
        ("seg", 180, "STILL", "hands off until the end"),
        ("cmd", "infer log off", 5, []),
        ("cmd", "infer", 10, []),
        ("cmd", "ui", 10, []),
        ("cmd", "fault", 10, []),
        ("cmd", "wifi stats", 10, []),
        ("cmd", "i2c stats", 10, []),
        ("cmd", "diag mem", 10, []),
        ("cmd", "log 80", 20, []),
        ("cmd", "uptime", 5, []),
        ("cmd", "version", 10, []),
        ("ask", "Did the board restart, freeze or stop updating the LCD at any time? (y/n, or what happened)"),
    ],
    # T5: >= 5 min with the Wi-Fi module held in reset.
    "m6_offline": [
        ("say", "T5 offline: 5.5 minutes with the Wi-Fi module switched OFF (held in reset), following the\n"
                "   timed lines. Same bag and dummy load as the soak. If you can, film the LCD with a phone for\n"
                "   the whole run (the video stays out of git; put a link in evidence/video_links.md)."),
        ("cmd", "wifi off", 10, []),
        ("cmd", "infer log on", 5, []),
        ("seg", 100, "STILL", "bag closed, hands off (Wi-Fi is off)"),
        ("seg", 10, "CARRIED", "pick the bag up and put it on your back"),
        ("seg", 60, "CARRIED", "walk normally"),
        ("seg", 10, "STILL", "put the bag down gently, hands off"),
        ("seg", 15, "ACCESS", "open the bag, reach in, rummage, close"),
        ("seg", 30, "STILL", "hands off"),
        ("seg", 15, "ROUGH", "dummy load: set the bag down hard / swing it"),
        ("seg", 90, "STILL", "put it down, hands off until the end"),
        ("cmd", "infer log off", 5, []),
        ("cmd", "infer", 10, []),
        ("cmd", "ui", 10, []),
        ("cmd", "wifi stats", 10, []),
        ("cmd", "diag mem", 10, []),
        ("cmd", "log 40", 15, []),
        ("ask", "Did the LCD keep showing the state (and change with what you did) the whole time? (y/n, or what happened)"),
        ("ask", "Video of the LCD recorded? (type: done / skip)", ["done", "skip"]),
    ],
    # T6: AC-8, every failure case the kit allows.
    "m6_fault": [
        ("say", "T6 controlled failures: board flat on the desk, hands off unless told. About 5 minutes,\n"
                "   one RESET press. Watch the bottom line of the LCD and the red LED."),
        ("cmd", "infer log on", 5, []),
        ("seg", 10, "STILL", "reference: hands off"),
        ("cmd", "fault inject imu", 5, []),
        ("seg", 12, "FAULT_IMU", "IMU reads now fail: watch the LCD bottom line"),
        ("ask", "Does the LCD show 'Sensor fault - IMU' (and is the red LED on)? (y/n)"),
        ("cmd", "fault", 10, []),
        ("cmd", "fault clear imu", 5, []),
        ("seg", 10, "STILL", "IMU fault cleared: watch the LCD"),
        ("ask", "Did the fault line go away and the state come back? (y/n)"),
        ("cmd", "fault inject als", 5, []),
        ("seg", 12, "FAULT_ALS", "light/proximity reads now fail: watch the LCD bottom line"),
        ("ask", "Does the LCD show 'Sensor fault - ALS/PS'? (y/n)"),
        ("cmd", "fault clear als", 5, []),
        ("seg", 10, "STILL", "ALS fault cleared: watch the LCD"),
        ("cmd", "fault inject aht", 5, []),
        ("seg", 12, "FAULT_AHT", "temperature/humidity reads now fail: the state should keep updating"),
        ("ask", "Does the LCD show 'Sensor fault - AHT' while the state keeps updating? (y/n, or what you saw)"),
        ("cmd", "fault clear aht", 5, []),
        ("seg", 10, "STILL", "AHT fault cleared"),
        ("cmd", "fault", 10, []),
        ("cmd", "sd", 15, []),
        ("ask", "Is the microSD slot empty (no card in the kit)? (y/n)"),
        ("cmd", "wifi on", 15, []),
        ("seg", 10, "STILL", "Wi-Fi module on"),
        ("cmd", "wifi off", 10, []),
        ("seg", 15, "STILL", "Wi-Fi lost (module held in reset): the LCD should keep updating"),
        ("cmd", "wifi on", 15, []),
        ("cmd", "wifi stats", 10, []),
        ("seg", 10, "STILL", "Wi-Fi back"),
        ("cmd", "infer", 10, []),
        ("reset", "RESET while classifying: the board should boot back into RUN by itself."),
        ("cmd", "infer log on", 5, []),
        ("seg", 10, "STILL", "after RESET: hands off"),
        ("cmd", "log 20", 15, []),
        ("cmd", "fault hang infer", 5, []),
        ("reboot", "The inference task stops checking in: the watchdog should reset the board within ~8 s.", 30),
        ("cmd", "fault", 10, []),
        ("cmd", "log 10", 15, []),
        ("cmd", "infer", 10, []),
        ("cmd", "diag mem", 10, []),
        ("ask", "After the watchdog reset, is the board back in RUN with the state updating? (y/n)"),
    ],
    # T5 part B: the board ran on a power bank with no laptop; read back what it logged.
    "m6_eventlog": [
        ("say", "T5 part B read-back: the board has just been plugged back into the laptop after its\n"
                "   power-bank run. Nothing to do on the board; this reads its event log (about 20 s)."),
        ("ask", "When did the power-bank run start and end (clock time), and what did you do with the bag?", "text"),
        ("cmd", "log 200", 40, []),
        ("cmd", "infer", 10, []),
        ("cmd", "version", 10, []),
    ],
    "m6_keys": _keys_plan(),
    # T8: every verified module reachable and live (DIAG pages + console checks).
    "m6_diag": [
        ("say", "T8 diagnostics: you walk through the 5 DIAG pages with the keys and answer what you see.\n"
                "   If you can, film the LCD while you do it (link in evidence/video_links.md). About 4 minutes."),
        ("cmd", "infer log off", 5, []),
        ("say", "Press UP once: DIAG page 1/5 'Sensors'. Tilt the board a little, then cover the left edge\n"
                "   below the screen with your hand, then press Enter here."),
        ("ask", "Page 1 Sensors: did the a/g numbers change when you tilted it, and light/ps when you covered it? (y/n)"),
        ("say", "Press RIGHT once: page 2/5 'Tasks'. Then press Enter here."),
        ("ask", "Page 2 Tasks: heap free/min and a task list with hwm and cpu% numbers? (y/n)"),
        ("say", "Press RIGHT once: page 3/5 'Inference'. Then press Enter here."),
        ("ask", "Page 3 Inference: model, window counts, feature/MLP timing and latency lines? (y/n)"),
        ("say", "Press RIGHT once: page 4/5 'System'. Then press Enter here."),
        ("ask", "Page 4 System: RTC, flash, event log, SD card, Wi-Fi, watchdog and uptime lines? (y/n)"),
        ("say", "Press RIGHT once: page 5/5 'I/O test'. Press DOWN once and watch: red LED, blue LED,\n"
                "   LED matrix red / green / blue, buzzer chirp. Then press Enter here."),
        ("ask", "I/O test: did you see the red LED, the blue LED, the matrix red, green and blue, and hear the chirp?\n"
                "   (y, or name what was missing)"),
        ("say", "Press UP once: back to RUN. Then press Enter here."),
        ("cmd", "diag sensors", 10, []),
        ("cmd", "i2c stats", 10, []),
        ("cmd", "fault", 10, []),
        ("cmd", "infer", 10, []),
        ("cmd", "ui", 10, []),
        ("cmd", "rtc", 10, []),
        ("cmd", "flash", 10, []),
        ("cmd", "sd", 15, []),
        ("cmd", "codec", 10, []),
        ("cmd", "ir loop", 10, []),
        ("cmd", "wifi on", 15, []),
        ("cmd", "wifi stats", 10, []),
        ("cmd", "wifi off", 10, []),
        ("cmd", "log 10", 15, []),
        ("cmd", "diag mem", 10, []),
        ("cmd", "version", 10, []),
        ("ask", "Video of the DIAG walk recorded? (type: done / skip)", ["done", "skip"]),
    ],
})


def ts():
    return datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]


def find_port():
    for p in serial.tools.list_ports.comports():
        if (p.vid, p.pid) == (0x0483, 0x374B) or "stlink" in (p.description or "").lower():
            return p.device
    return None


class Board:
    """Reads lines from the board, timestamps them, writes them to the log.

    A background thread reads the port all the time, so lines keep their
    arrival time while the operator is answering a prompt (the M6 analysis
    lines board output up with the timed steps). `[win]` lines (2 per second)
    go to the log but not to the screen; seg steps show the latest state."""

    def __init__(self, port, log_fp):
        self.ser = serial.Serial(port, BAUD, timeout=0.05)
        self.log = log_fp
        self.partial = ""
        self.partial_t = None
        self.last_rx = time.time()
        self.lines = []
        self.last_win = ""
        self._dec = codecs.getincrementaldecoder("utf-8")("replace")
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._reader, daemon=True)
        self._thread.start()

    def close(self):
        self._stop.set()
        self._thread.join(1.0)
        self.ser.close()

    def _reader(self):
        while not self._stop.is_set():
            try:
                data = self.ser.read(4096)
            except serial.SerialException as e:
                print("!! serial read failed: %s" % e)
                return
            if data:
                self._feed(data)

    def _feed(self, data):
        self.last_rx = time.time()
        for ch in self._dec.decode(data):
            if self.partial_t is None:
                self.partial_t = ts()
            if ch == "\n":
                line = self.partial.rstrip("\r")
                self.log.write("%s > %s\n" % (self.partial_t, line))
                self.log.flush()
                i = line.find("[win]")
                if i < 0:
                    print("   | " + line)
                else:
                    self.last_win = line[i:]
                    if line[:i].strip(" >"):
                        print("   | " + line[:i])
                self.lines.append(line)
                self.partial = ""
                self.partial_t = None
            else:
                self.partial += ch

    def poll(self):
        """The reader thread does the work; this only paces the wait loops."""
        time.sleep(0.02)

    def state_text(self):
        """Latest smoothed state from the [win] lines, for the operator."""
        kv = dict(p.split("=", 1) for p in self.last_win.split() if "=" in p)
        if not kv:
            return "no window yet (is `infer log on`?)"
        st = kv.get("state", "-")
        if st == "-":
            return "no state (no model, or not classified yet)"
        try:
            conf = "%d %%" % round(float(kv.get("conf", "0")) * 100)
        except ValueError:
            conf = "?"
        return "%s %s%s%s" % (st, conf, " (uncertain)" if kv.get("unc") == "1" else "",
                               " FAULT" if kv.get("fault", "0x0") != "0x0" else "")

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

    def prompt_after(self, start, cmd):
        """The prompt came back after the echo of cmd, even if background lines
        (M5: [event], [key]) were printed after it on the same line."""
        seen_echo = False
        for line in self.lines[start:] + [self.partial]:
            if not seen_echo:
                seen_echo = line.rstrip().endswith(cmd)
                continue
            if line.startswith("> "):
                return True
        return False


def after_boot(board, n_lines):
    """M5+ firmware: wait for the boot self-test to reach RUN, stop the [win] lines."""
    if board.wait_for(lambda: any("BOOT -> " in l for l in board.lines[max(0, n_lines - 5):]), 15):
        board.send("infer log off")
        board.wait_for(lambda: False, 1.0)
    board.wait_for(board.at_prompt, 3)


def run_seg(board, step, cues, notes):
    """Timed step with an expected label: start/end PC times go to the notes CSV."""
    secs, expected, text = step[1], step[2], step[3]
    trial = step[4] if len(step) > 4 else ""
    print("\n   ##########  NOW: %s (%d s)  ##########\n" % (text, secs))
    t_start = ts()
    cues.write("%s cue: %s (%d s)\n" % (t_start, text, secs))
    cues.flush()
    t0 = time.time()
    tick = 2 if secs <= 20 else 5
    next_tick = t0 + tick
    while time.time() - t0 < secs:
        board.poll()
        left = secs - (time.time() - t0)
        if time.time() >= next_tick and left > 0.5:
            print("   .. %3d s left | board: %s" % (round(left), board.state_text()))
            next_tick += tick
    t_end = ts()
    print("   ##########  STOP  ##########")
    cues.write("%s cue: stop\n" % t_end)
    ok = 1
    if trial:
        a = input("   Enter = trial done as described, x = something went wrong: ").strip().lower()
        if a.startswith("x"):
            ok = 0
            why = input("   What went wrong (a few words)? ").strip()
            cues.write("%s answer: %s went wrong: %s\n" % (ts(), trial, why))
    notes.write("%s,%s,%s,%s,%d\n" % (trial, t_start, t_end, expected, ok))
    notes.flush()
    cues.flush()


def run_plan(name, port, force, out_dir):
    stem = name if name.startswith(("m5_", "m6_")) else "m2_%s" % name
    out = os.path.join(out_dir, "%s.log" % stem)
    cues_path = os.path.join(out_dir, "%s_cues.txt" % stem)
    notes_path = os.path.join(out_dir, "%s_notes.csv" % stem)
    if os.path.exists(out) and not force:
        print("%s already exists. Use --force to overwrite it." % out)
        return False
    has_seg = any(s[0] == "seg" for s in PLANS[name])

    print("\n=== %s: capturing to %s" % (name.upper(), out))
    with open(out, "w", encoding="utf-8", newline="\n") as log, \
            open(cues_path, "w", encoding="utf-8", newline="\n") as cues, \
            open(notes_path if has_seg else os.devnull, "w", encoding="utf-8", newline="\n") as notes:
        cues.write("# operator cues shown by tools/capture_m2.py (PC time, %s)\n" % datetime.date.today())
        notes.write("trial,start,end,expected,ok\n")
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
            # M5 firmware: the UI runs the boot self-test (it also resets the
            # Wi-Fi module) and then enters RUN; wait for that before typing,
            # and stop the per-window lines so commands are easy to follow.
            after_boot(board, 0)
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
                    after_boot(board, len(board.lines))
                    cues.flush()
                    continue
                if step[0] == "reboot":
                    _, text, limit = step
                    print("\n>> " + text + " (no key press needed)")
                    cues.write("%s cue: wait for the board to reboot by itself (%s)\n" % (ts(), text))
                    n_ready = sum("[cli] ready" in l for l in board.lines)
                    if board.wait_for(lambda: sum("[cli] ready" in l for l in board.lines) > n_ready, limit):
                        cues.write("%s cue: reboot seen\n" % ts())
                        after_boot(board, len(board.lines))
                    else:
                        print("!! no reboot seen within %d s" % limit)
                        cues.write("%s cue: NO reboot within %d s\n" % (ts(), limit))
                    cues.flush()
                    continue
                if step[0] == "seg":
                    run_seg(board, step, cues, notes)
                    continue
                if step[0] == "wait":
                    _, secs, text = step
                    print("\n   ##########  NOW: %s (%d s)  ##########\n" % (text, secs))
                    cues.write("%s cue: %s (%d s)\n" % (ts(), text, secs))
                    board.wait_for(lambda: False, secs)
                    print("   ##########  STOP  ##########")
                    cues.write("%s cue: stop\n" % ts())
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
                    if not pending and len(board.lines) > start_lines and (
                            board.at_prompt() or board.prompt_after(start_lines, cmd)):
                        board.wait_for(lambda: False, 0.3)
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
    ap.add_argument("sensor", choices=list(PLANS) + ["all", "io", "m6_t1", "find-als", "check-als"],
                    help="all = imu, als, env; io = leds, buzzer, keys; m6_t1 = the four T1 runs; find-als: live "
                         "light/proximity readout to locate the sensor (writes no files); "
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

    groups = {"all": ["imu", "als", "env"], "io": ["leds", "buzzer", "keys"],
              "m6_t1": ["m6_t1_" + c for c in T1_CLASSES]}
    names = groups.get(args.sensor, [args.sensor])
    ok = all(run_plan(n, port, args.force, args.out_dir) for n in names)
    if ok:
        print("\nAll done. Tell the agent the evidence is in.")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
