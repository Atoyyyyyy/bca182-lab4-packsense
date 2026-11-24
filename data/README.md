# data/

Anonymized raw sensor captures from the RT-Spark and their manifest (PRD §7.1).

| Path | What |
|---|---|
| `raw/S<nn>.csv` | one file per recording session, written by `tools/capture.py dump` |
| `manifest.csv` | one row per (session, label): who/where/what, codes only |

Rules: operator codes only (P1, P2, …), never names. Bag (B1, B2, …) and
location (L1, L2, …) codes are explained in the table below, not in the files.
Files containing anything private use the `*_private*` suffix, which is
git-ignored.

## How the data is recorded

The kit has no microSD card (`docs/hardware/exceptions.md` #1), so the board
records labelled trials to its 8 MB SPI flash (about 43 minutes at 100 Hz) and
`tools/capture.py` downloads them afterwards. The board can therefore sit in
the bag on a USB power bank, with no laptop attached. The step-by-step session
procedure is in [`docs/test_sheets/m3_capture.md`](../docs/test_sheets/m3_capture.md).

On the board (CAPTURE mode: hold **UP** while pressing **RESET**):

| Key | Does | Feedback |
|---|---|---|
| LEFT / RIGHT | choose the label | 1–4 beeps = STILL, CARRIED, ROUGH, ACCESS; LED matrix + LCD colour = blue, green, orange, magenta |
| UP | start a trial | 5 s countdown (ticks, LEDs blink), long beep = recording, red LED on, LEDs count the time left |
| (none) | trial ends by itself | double beep after 30 s (STILL, CARRIED) or 15 s (ROUGH, ACCESS) |
| DOWN | stop early / cancel the countdown | double beep (stop) or one long beep (cancel) |

## CSV schema (`raw/S<nn>.csv`)

`session_id,trial_id,label,t_ms,ax,ay,az,gx,gy,gz,als,ps,ps_valid,temp,rh,flags`

| Column | Unit / meaning |
|---|---|
| `session_id` | `S01`, `S02`, … (global, assigned at download) |
| `trial_id` | 1, 2, … within the session |
| `label` | `STILL`, `CARRIED`, `ROUGH`, `ACCESS` (PRD §2.3) |
| `t_ms` | board tick (ms since boot) when the IMU read finished; 10 ms apart |
| `ax ay az` | m/s², ICM-20608 ±4 g, stored at 1/800 m/s² |
| `gx gy gz` | deg/s, ±500 dps, stored at 1/60 deg/s; no bias calibration applied |
| `als` | lux (AP3216C range 1, 0.35 lux/count). Read at 20 Hz, but the chip updates only about every 225 ms, so values repeat |
| `ps` | proximity, 10-bit raw, higher = closer |
| `ps_valid` | 0 when the IR-overflow flag was set or the read failed |
| `temp rh` | °C, %RH from the AHT (1 Hz, context only, not a classifier input) |
| `flags` | bit 0 IMU read error, 1 IMU axis saturated, 2 ALS/PS read error, 3 ALS/PS read on this sample, 4 AHT error, 5 timestamp gap > 30 ms |

## Labels (PRD §2.3), what to do during a trial

| Label | During the whole trial | Trial length |
|---|---|---|
| `STILL` | bag closed on a surface, nobody touches it. Vary the surface between trials (floor, desk, chair) | 30 s |
| `CARRIED` | bag worn on the back (or held by the handle, one trial per session), walking normally in a corridor | 30 s |
| `ROUGH` | keep handling it roughly: set it down hard, swing it, shake it, toss it onto a cushion or bed from ≤ 50 cm. **Dummy load only, never a laptop. No real drops.** | 15 s |
| `ACCESS` | bag on a surface: open the compartment, reach in, move the items around; keep rummaging, and close it when about 3 LEDs are left | 15 s |

## Variation to cover (PRD §7.1)

At least 2 bags, 2 carriers, empty vs. a dummy weight (books, a water bottle),
bright vs. dim rooms, different set-down surfaces. Record which in the
manifest; codes are listed here:

| Code | Meaning |
|---|---|
| B1, B2 | bags (describe: type, size) — TODO when recorded |
| L1, L2 | locations (describe: room type, bright/dim) — TODO when recorded |
| P1, P2 | operators (team members; never written as names) |

## Volume

One session = 3 STILL + 3 CARRIED + 4 ROUGH + 4 ACCESS trials (5 min of
recording, about 15 min including handling). After windowing (2 s windows,
0.5 s hop, 1 s trimmed from each end of a trial) that is about 159 / 159 / 92 /
92 windows per session. Four sessions use 20 of the 43 minutes the flash holds.
Three sessions train, a fourth (different bag or carrier) is the test set,
which meets the PRD gate (≥ 30 train and ≥ 10 test windows per class from
separate sessions) about three times over. `ml/prepare.py` checks the gate.

The ≥ 20 independent trials per class for the final system test (PRD §9) are
recorded later, in M6, and are never used for training or tuning.
