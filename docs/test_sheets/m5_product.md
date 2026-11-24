# M5 test sheet: on-device inference + product firmware

Covers the M5 hand-off of PRD §11 (*flash; run `selftest model` →
`evidence/m5_parity.log`; quick functional check per class*) and gives the
first data for AC-3 (parity), AC-4 (timing, sense-to-feedback), AC-5 (keys)
and AC-8 (fault shown, system keeps running). The full M6 tests (15-min soak,
5-min offline, all fault cases) have their own sheets.

## Before you start

1. The M3 sessions are recorded and downloaded (`docs/test_sheets/m3_capture.md`),
   and the agent has run the pipeline and committed the exported model:
   `ml/prepare.py` → `ml/train.py` → `ml/evaluate.py` → `ml/export_c.py`
   (writes `src/model/model_params.h`, `model_weights.h`,
   `test/golden/golden_vectors.h`).
2. The working tree is clean (`git status` shows nothing to commit). The
   capture tool warns if the firmware banner says `-dirty`.
3. Close every other serial monitor.

## 1. Parity and timing (no hands, about 1 minute)

```
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tools\capture_m2.py m5_parity --flash
```

Press RESET when asked. The board recomputes the 50 golden test windows,
compares them with the PC model and prints the timing, then classifies live
for 30 s while the board lies still.

Pass: the log contains `[selftest] AC-3 (argmax match >= 98 %): PASS`.
The agent then runs `python tools/latency_stats.py evidence/m5_parity.log --md`
for the timing table (features, MLP, window close → feedback).

## 2. Functional check (about 4 minutes, with the bag)

```
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tools\capture_m2.py m5_func
```

The board goes in the bag the way it was for the recordings. The tool tells
you, step by step: leave the bag still, wear it and walk, set it down hard
(**dummy load only**, no laptop), open it and reach in, arm GUARD and open it
again (alarm, then one key press), then a simulated IMU fault. After each step
it asks what the LCD showed. Answer what you actually saw: a wrong state is a
result too, not a failure of the test.

Files: `evidence/m5_func.log` (board output, including one `[win]` line per
window and every `[event]`) and `evidence/m5_func_cues.txt` (your answers,
with times).

## What the screen shows (RUN / GUARD)

| Area | Content |
|---|---|
| Header | mode (blue RUN, red GUARD), `MUTE` if muted, clock (`--:--:--` if the RTC is not set) |
| Centre | smoothed state in large letters (blue STILL, green CARRIED, orange ROUGH, magenta ACCESS; `?` = confidence < 50 %), confidence under it |
| "why" lines | motion sd (g), jerk peak (m/s²), light (lux), proximity, tilt (°) of the last window |
| Climate | temperature and RH, or `HOT/HUMID! check laptop` |
| Alert line | `Rough handling detected` (4 s) or `Sensor fault - IMU / ALS/PS / AHT / gap` |
| Footer | the key just pressed (echo), otherwise the key hints |

LED matrix: state colour (dim white = uncertain); one red LED = GUARD armed;
all red flashing = alarm. Red indicator LED: alarm or sensor fault. Blue
indicator LED: toggles once per window (inference alive).

## Keys

| Mode | Key | Action |
|---|---|---|
| RUN / GUARD | LEFT held ~1 s | arm / disarm GUARD |
| RUN / GUARD | UP | DIAG pages |
| RUN / GUARD | DOWN held ~1 s | mute / unmute the buzzer (alerts stay visible) |
| RUN / GUARD | RIGHT | clear the alert line |
| alarm | any key | acknowledge (stops buzzer and red screen; logged with the response time) |
| DIAG | LEFT / RIGHT | previous / next page: Sensors, Tasks, Inference, System, I/O test |
| DIAG | UP | back to RUN / GUARD |
| DIAG (I/O test) | DOWN | red LED, blue LED, matrix R/G/B, buzzer chirp |

Hold UP while pressing RESET to start in CAPTURE mode (M3), as before.

## Useful console commands

| Command | Does |
|---|---|
| `mode [run\|guard\|capture\|diag]` | show or change the mode |
| `infer`, `infer log on\|off` | inference counts and timing; the per-window `[win]` lines |
| `ui` | mode, alarm, mute, climate; window close → feedback latency |
| `selftest model [v]` | golden-vector parity (AC-3), `v` = error per feature |
| `fault`, `fault inject\|clear imu\|als\|aht\|all` | sensor health; simulate a NACK (AC-8) |
| `fault hang <task>` | that task stops checking in → watchdog reset (shows the IWDG) |
| `alarm test` | raise the GUARD alarm without a detection (M6 T7: timing ten acknowledges) |
| `climate [set <°C> <RH%> <hold_s>]` | climate warning thresholds (default 40 °C / 80 % / 30 s) |
| `log [n]`, `log clear yes` | the flash event log |
| `wifi off`, `wifi on` | hold the RW007 in reset / start it (offline test, AC-7) |
| `diag mem` | heap, per-task stack HWM, CPU share |
