# PackSense system test plan (M6)

PRD §9 lists the tests; this file says exactly how each one is run, how it is
scored and which file proves it. The step-by-step operator sheet is
[`test_sheets/m6_tests.md`](test_sheets/m6_tests.md).

**Rule (PRD §0.3):** every number in the *Result* column comes from a script
run on a log in `evidence/`. Until then it says `TODO(measure)`.

## Under test

| Item | Value |
|---|---|
| Firmware | `0.5.0-m5` or later, built from a clean tree (the banner must not say `-dirty`) |
| Model | the trained MLP exported by `ml/export_c.py` after M3/M4: TODO(measure) version hash from `infer` |
| Board | RT-Spark HW1.1, as audited in `docs/hardware/inventory.md` |
| Set-up | board flat against the bag's back panel, LCD facing into the compartment (same as the M3 recordings); USB cable from the ST-LINK port out through the corner of the zipper to the laptop, which logs the console |
| Gate | M5 parity done first: `evidence/m5_parity.log` with `AC-3 ... PASS` |

The laptop tether is needed because the `[win]` lines (one per 2 s window) are
the record of what the board decided. The product itself needs no laptop (T5
part B shows this).

## Test matrix

| Test | Criterion | Run with | Pass condition | Evidence | Analysis | Result |
|---|---|---|---|---|---|---|
| T1 Functional per class | AC-1, AC-2 on new trials | `capture_m2.py m6_t1_still` / `_carried` / `_rough` / `_access` (20 trials each, sheet `test_sheets/T1.csv`) | ≥ 20 scored trials per class; observed vs expected table and per-class accuracy reported (targets: mean ≥ 0.85, no class < 0.75; the model beats the baseline) | `evidence/m6_t1_<class>.log`, `_notes.csv`, `_cues.txt` | `tools/t1_results.py evidence/m6_t1_*.log --sheet docs/test_sheets/T1.csv --md` | TODO(measure) |
| T2 Latency | AC-4 | every RUN log has one `[win]` line per window with `t_feat_us`, `t_inf_us`, `lat_ms` | ≥ 50 timed inferences; median and p95 reported; window close → feedback < 500 ms | the T1 and T4 logs, `evidence/m5_parity.log` | `tools/latency_stats.py evidence/m6_t1_*.log evidence/m6_soak.log --md` | TODO(measure) |
| T3 Memory | AC-9 | `pio run -t clean`, then `pio run -v > evidence/size.txt`; `diag mem` at ~10 min of the soak | Flash < 70 %, SRAM and CCM RAM < 80 %, every task HWM ≥ 64 words | `evidence/size.txt`, `diag mem` dumps in `m6_soak.log` | `tools/size_report.py evidence/size.txt --md`; HWM: `tools/analyze_soak.py` | TODO(measure) |
| T4 Soak | AC-6 | `capture_m2.py m6_soak` (16 min of scripted mixed use, Wi-Fi module on) | ≥ 15 min, 0 resets, 0 watchdog reports; missed reads / rejected windows counted; false alerts counted | `evidence/m6_soak.log`, `_notes.csv` | `tools/analyze_soak.py evidence/m6_soak.log --notes evidence/m6_soak_notes.csv` | TODO(measure) |
| T5 Offline | AC-7 | A: `capture_m2.py m6_offline` (5.5 min, RW007 held in reset). B (optional): ≥ 5 min on a power bank, no laptop, then `capture_m2.py m6_eventlog` (`log 200`) | inference and feedback unchanged: same window rate, no rejected windows, same latency as T4; LCD kept updating (video); B: state changes logged throughout the power-bank boot | `evidence/m6_offline.log`, `_notes.csv`, `evidence/m6_eventlog.log`, video link in `evidence/video_links.md` | `analyze_soak.py` + `latency_stats.py` on T4 and T5, side by side | TODO(measure) |
| T6 Controlled failure | AC-8 | `capture_m2.py m6_fault` | each case: fault shown on the LCD, invalid windows not classified, no crash, other tasks keep running, recovery after the cause is removed | `evidence/m6_fault.log`, `_cues.txt` (operator answers) | `analyze_soak.py evidence/m6_fault.log` + the case table below | TODO(measure) |
| T7 Input control | AC-5 | `capture_m2.py m6_keys` | acknowledge, arm, disarm, mute, unmute, DIAG in / out: each 10/10, every response < 150 ms | `evidence/m6_keys.log`, `_notes.csv` | `tools/t7_keys.py evidence/m6_keys.log --md` | TODO(measure) |
| T8 Diagnostics | PRD §5.1 DIAG, inventory | `capture_m2.py m6_diag` | all 5 DIAG pages live; every verified module answers its console check | `evidence/m6_diag.log`, `_cues.txt`, video link | read the log against `docs/hardware/inventory.md` | TODO(measure) |
| T9 Privacy / safety / accessibility | PRD §3 risks | review [`test_sheets/T9_checklist.md`](test_sheets/T9_checklist.md) | every item checked on the board and signed | the filled-in checklist | — | TODO (H) |

Order and time: T3 build (2 min) → T7 (5 min) → T8 (4 min) → T6 (5 min) →
T1 four runs (about 45 min with set-up) → T4 (17 min) → T5 (6 min, plus the
optional part B). About 1.5 hours, in one or two sittings.

## How each test is scored

### T1 functional per class

The 80 trials in [`test_sheets/T1.csv`](test_sheets/T1.csv) (S01–S20, C01–C20,
R01–R20, A01–A20) vary the bag, the load, the person, the surface and the room
light like the M3 sessions, but are recorded later and are **never** used for
training or tuning (PRD §7.1). Each trial is a fresh event: the bag is put down,
picked up or opened again for every trial.

| Class | Trial length | Windows scored | Observed = |
|---|---|---|---|
| STILL, CARRIED (steady) | 12 s / 20 s | those that arrive from start + 3 s to the end (the first window that holds only trial data closes 2 s after the start, then majority-of-3 adds about 1 s) | the most frequent **shown** state |
| ROUGH, ACCESS (events) | 8 s / 12 s | from the start to end + 2 s | the expected class if the board showed it (not uncertain) at least once, else the most frequent shown state |

*Shown state* is what the LCD shows: the smoothed state when its confidence is
≥ 0.5, `UNCERTAIN` (the LCD's `?`) below that, `INVALID` when the window was
rejected for a sensor fault. In STILL and CARRIED trials, every switch of the
shown state to ROUGH or ACCESS is a **false alert** (it would chirp or log an
access event); these are counted separately. ACCESS trials run in RUN mode
(the same classifier decides in GUARD; only the response differs), so the
alarm sound and the red screen do not change what the light sensor sees.

The same rules score the on-device threshold baseline (`base=`, one decision
per window, unsmoothed) and the raw MLP output (`raw=`) from the same windows,
so model and baseline are compared on identical data (AC-2 on new trials).
A trial the operator marks as gone wrong (`x` after STOP) is listed and left
out; if that leaves fewer than 20 in a class, those trials are repeated.

### T2 latency

`t_feat_us` (features) and `t_inf_us` (MLP) are DWT cycle counts measured by
InferTask; `lat_ms` is the window's last sample → LEDs and LCD updated
(UiTask). `latency_stats.py` reports n, median, p95 and max per quantity.
`selftest model` adds the golden-vector timing (`m5_parity.log`).

### T3 memory

`size_report.py` takes the linker's region table (it includes the reserved
heap and stack sections, so it is the stricter figure) and checks FLASH < 70 %,
RAM (128 KB SRAM) < 80 % and CCMRAM (64 KB, all task stacks) < 80 %.
`analyze_soak.py` prints the lowest stack high-water mark per task over every
`diag mem` dump in the soak; AC-9 fails if any task is below 64 words.

### T4 soak

`m6_soak` runs 16 minutes of timed steps (rest, walking, two rough handlings,
two openings, one of them guarded with the alarm and an acknowledge, DIAG
pages, mute) and writes each step's expected state to the notes file. The RW007
is powered and initialised for the whole run (the *online* condition for T5).
`analyze_soak.py` counts boots and reset causes, watchdog reports, sensor
DEGRADED lines, rejected windows, missing window numbers, the longest gap
between windows, events by type, and ROUGH / ACCESS events outside a step that
expected them (false alerts).

### T5 offline

Part A repeats a shorter mix with the RW007 held in reset (`wifi off`). It
passes when its window rate (2 per second), rejected-window count and
`lat_ms` / `t_feat_us` distributions match T4 and the LCD kept updating. The
product never uses the network (Wi-Fi is not on the inference path; PRD §6.2),
so this shows that switching the radio off changes nothing.
Part B (optional, stronger): the board runs from a power bank with no laptop
for ≥ 5 minutes of mixed use; afterwards `log 200` lists the state changes
of that boot with their uptimes, which shows it kept classifying on its own.

### T6 controlled failure

| Case | How | Expected |
|---|---|---|
| (a) IMU / ALS / AHT failure | `fault inject imu` (then `als`, `aht`): the driver reports every read as a NACK, as if the sensor were unplugged. The on-board sensors are soldered, so a physical disconnect is not possible | LCD `Sensor fault - <name>`, red LED, error beeps; IMU and ALS: windows rejected (`valid=0`), not classified; AHT: classification continues, climate rule paused; DEGRADED after 3 failed reads, re-init every 5 s; after `fault clear` the sensor recovers |
| (b) SD card absent | the kit has no microSD card (`docs/hardware/exceptions.md` #1) | boot self-test `SD card absent`, `sd` reports `NOT_PRESENT`; the event log uses the SPI flash; nothing else affected |
| (c) Wi-Fi loss / reconnect | `wifi on` → `wifi off` (module held in reset) → `wifi on` | windows continue at the same rate throughout; the RW007 re-initialises; the SPI flash event log on the same SPI2 bus keeps working |
| (d) Reset during RUN | RESET pressed while classifying | boots back into RUN by itself; the event log continues with the next boot number |
| (e) Hung task | `fault hang infer`: InferTask stops checking in | `[wdg] check-in missing ... Infer`, IWDG reset within about 8 s (the 2 s check-in window is missed, then the 4 s IWDG timeout runs out), the next boot reports the watchdog and logs a `WDG` event naming Infer |

### T7 input control

`m6_keys` asks for each action ten times; `t7_keys.py` matches every request
with the board's own lines in that step. `ack`: the alarm is raised with
`alarm test` (a console hook that raises the GUARD alarm without a detection,
so ten acknowledges do not need ten bag openings; the real ACCESS → alarm
path is exercised in T4 and in `m5_func`), and the response time is the
acknowledging press → LCD echo. Hold and press actions: the response time is
the key's release edge → action done (screen redrawn), because a long press can
only be recognised when the key is let go; the press → echo time is reported
too.

### T8 diagnostics

The operator walks DIAG pages 1–5 (Sensors, Tasks, Inference, System, I/O
test) and answers what each page shows; the I/O test drives the red and blue
LED, the LED matrix in red / green / blue and a buzzer chirp. The plan then
runs each module's console check (`diag sensors`, `i2c stats`, `fault`,
`infer`, `ui`, `rtc`, `flash`, `sd`, `codec`, `ir loop`, `wifi on` / `wifi stats` /
`wifi off`, `log`, `diag mem`). Pass: every module marked verified in
`inventory.md` is live on a page or answers its command.

### T9 privacy, safety, accessibility

A checklist with the agent's code references in
[`test_sheets/T9_checklist.md`](test_sheets/T9_checklist.md); the team checks
each item on the board and signs it. The result goes into the report's
risks section.

## Results

Filled in only from the logs named above.

| Test | Result | Source |
|---|---|---|
| T1 | TODO(measure) | `t1_results.py` |
| T2 | TODO(measure) | `latency_stats.py` |
| T3 | TODO(measure) | `size_report.py`, `analyze_soak.py` |
| T4 | TODO(measure) | `analyze_soak.py` |
| T5 | TODO(measure) | `analyze_soak.py`, `latency_stats.py` |
| T6 | TODO(measure) | `m6_fault.log`, cues |
| T7 | TODO(measure) | `t7_keys.py` |
| T8 | TODO(measure) | `m6_diag.log`, cues |
| T9 | TODO (H) | checklist |

## Improvements supported by the test data

PRD §9 asks for at least two. They are written only after the logs exist,
each with the number that motivates it (for example the most frequent
confusion in the T1 matrix, or the false-alert count in T4). TODO(measure).

## Known limits of these tests

- The USB tether leaves the zipper slightly open at one corner; the M3
  recordings were made with the zipper fully closed and a power bank inside.
  This mainly affects the light reading in STILL / CARRIED trials.
- No stairs, running or vehicles while tethered (safety); CARRIED covers level
  walking only.
- Sensor failures are simulated at the driver (NACK), not by unplugging; the
  SD case is the absent card only; Wi-Fi never joins a network because the
  product has no network function.
- All operators are team members (codes P1–P3); no outside users.
