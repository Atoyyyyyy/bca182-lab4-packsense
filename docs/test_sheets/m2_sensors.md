# M2 test sheet: I2C sensors (IMU, light/proximity, temperature/humidity)

**Who:** H (a person at the board). **Firmware:** `0.2.0-m2` or later, built
from a clean tree (the banner must not say `-dirty`).
**Result files:** `evidence/m2_imu.log`, `evidence/m2_als.log`,
`evidence/m2_env.log`, checked with `tools/m2_sensor_stats.py`.

## Before you start

```powershell
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
& $pio run -t upload
```

For each module below:

1. Start the monitor: `& $pio device monitor`. Each run writes a new
   `logs\device-monitor-*.log`.
2. Press **RESET** on the board and wait for `[cli] ready, type 'help'`.
3. Type the commands listed for that module, one per line, then Enter.
4. Press **Ctrl+C**, then move the log:
   ```powershell
   Move-Item (Get-ChildItem logs\device-monitor-*.log | Sort-Object LastWriteTime | Select-Object -Last 1) evidence\m2_<module>.log
   ```
   Replace `<module>` with `imu`, `als` or `env`.

A stream stops early if you press any key, so keep your hands off the keyboard
while one runs.

## 1. IMU (ICM-20608-G) → `evidence/m2_imu.log`

Put the board **flat on the table, components up**, and do not touch it until step d.

| Step | Type | What you should see |
|---|---|---|
| a | `i2c scan` | four devices: 0x10, 0x1E, 0x68 on i2c2, 0x38 on soft |
| b | `imu` | `init OK who_am_i=0xAF`, one reading |
| c | `imu cal` | `cal OK`, gyro bias, `mean|a|` near 9.8 |
| d | `imu stream 1000` | 10 s of CSV with the board **still** |
| e | `imu stream 500` | 5 s: **pick the board up and tilt it** slowly onto each edge |
| f | `diag mem` | task table |

## 2. Light / proximity (AP3216C) → `evidence/m2_als.log`

| Step | Type | What to do during the stream |
|---|---|---|
| a | `als` | one reading |
| b | `als stream 40` | 10 s: first 3 s leave it alone, then **cover the sensor with your hand** (about 1 cm away), then **shine a phone torch** at it |
| c | `i2c stats` | error counters (expect 0) |

Write down what you did and when (e.g. "covered at ~3 s, torch at ~7 s"). The
log should show lux falling and `ps` rising when covered, and lux rising under
the torch.

## 3. Temperature / humidity (AHT) → `evidence/m2_env.log`

| Step | Type | What to do during the stream |
|---|---|---|
| a | `env` | reading, `chip=...` (AHT2x or AHT1x), status byte |
| b | `env stream 30` | 30 s: after ~10 s, **breathe gently on the sensor** once or twice |
| c | `i2c stats` | error counters |

Humidity should rise for a few seconds after you breathe on it, then fall back.

Also **read the marking on the temperature/humidity chip** (U8 on the
schematic) and write it in `docs/hardware/inventory.md`, row 3. The
firmware's `chip=` line tells AHT2x from AHT1x by its checksum byte; the
marking settles which part it is.

## After capturing

Tell the agent. It runs:

```bash
python tools/m2_sensor_stats.py evidence/m2_imu.log
python tools/m2_sensor_stats.py evidence/m2_als.log
python tools/m2_sensor_stats.py evidence/m2_env.log
```

and records the intervals, offsets and calibration notes in `docs/`.
