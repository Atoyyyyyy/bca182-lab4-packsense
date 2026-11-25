# PackSense: edge-AI smart backpack insert (RT-Spark / STM32F407)

PackSense sits in a backpack's laptop compartment. It classifies what is happening
to the bag (**STILL · CARRIED · ROUGH · ACCESS**) with a small neural network
running **on the STM32F407**, warns about heat/humidity, and alerts the owner
locally through an LCD, LEDs, an LED matrix and a buzzer. It needs no phone and
no Internet.

BCA182 Embedded Systems Programming, Lab Activity No. 4, MSU-IIT CCS.
Team: Sherie Grace L. Lupoz · Renato G. Beroy · Binyusoph P. Sansarona
([authors and contributions](#authors-and-contributions)).

> **Status:** M1 done. M2: every module verified on the board with evidence
> logs except the Wi-Fi capture. M3 capture firmware/tools and the M4 ML
> pipeline + C inference are written and checked (synthetic smoke test only;
> no real data yet). M5 product firmware (all PRD §6.3 tasks, modes, alerts,
> event log, watchdog) runs on the board with a placeholder model. M6 test
> plan, guided test runs and analysis scripts, and the M7 report draft and
> demo script are written; their results wait for real data. **Next:**
> record the M3 sessions, then train/export the real model.
> See [Milestones](#milestones).

## Hardware

- RT-Thread **RT-Spark (星火1号)**, STM32F407ZGT6, schematic Rev HW1.1
- On-board ICM-20608-G IMU, AP3216C light/proximity, AHT21 temp/RH, 1.3" ST7789V3 LCD,
  SK6805 LED matrix, buzzer, keys, microSD, W25Q128 flash, RW007 Wi-Fi, ST-LINK/V2.1
- Pin map with schematic citations: [`docs/hardware/pinmap.md`](docs/hardware/pinmap.md)
- Inventory / verification: [`docs/hardware/inventory.md`](docs/hardware/inventory.md)

## Software stack

VS Code + PlatformIO (`platform = ststm32`, `framework = stm32cube`), STM32Cube
HAL/LL, FreeRTOS (vendored, version pinned), Python + scikit-learn for training.
The model is exported to plain C. No Arduino, RT-Thread OS or Zephyr.

## Repository layout

```
boards/       custom RT-Spark PlatformIO board manifest       (M1)
linker/       linker script with .ccmram                       (M1)
include/      FreeRTOSConfig.h, HAL conf, app config, secrets.example.h
lib/          vendored FreeRTOS, FatFs                         (M1/M2)
src/          main, drivers/, dsp/, model/, app/, ui/          (M1–M5)
test/golden/  golden vectors for `selftest model` (generated)  (M4)
ml/           data prep, features, baseline, training, export (M3–M4)
tools/        capture, latency/soak analysis, hw-doc fetcher
data/         anonymized raw captures + manifest               (M3)
docs/         design, hardware, task table, test plan, report
evidence/     real logs only: build, boot, diag, tests         (M1–M6)
```

## Getting started

Needs VS Code + the PlatformIO extension (or PlatformIO Core). On Windows, if
`pio` is not on your PATH, open a **PlatformIO Core CLI** terminal from the
PlatformIO sidebar, or call `$env:USERPROFILE\.platformio\penv\Scripts\pio.exe`.
The first build downloads the pinned `ststm32@20.0.0` platform.

```bash
# 1. Get the hardware documents (not redistributed in git)
python tools/fetch_hw_docs.py

# 2. Build
pio run

# 3. Flash over the onboard ST-LINK (USB cable to the board's ST-LINK port)
pio run -t upload

# 4. Serial console: 115200 8N1 on the ST-LINK virtual COM port.
#    Lines are timestamped and also saved to logs/device-monitor-*.log.
pio device monitor
```

### What a good boot looks like

The board prints a banner (git revision, reset cause, clock source, PLL
settings read back from RCC, bus clocks, HAL/FreeRTOS versions), then
`[cli] ready, type 'help'` and `[rtos] scheduler started`. Since M5 the UI task
then runs the boot self-test (LCD list + `[selftest] <part> OK|absent|FAIL`
lines, about 2 s), starts the watchdog and enters RUN: the LCD shows the state,
its confidence and the "why" values, and one `[win] ...` line per 0.5 s goes to
the console (`infer log off` stops them). Without a trained model the state reads
`NO MODEL` and nothing is classified. Keys, screens and commands:
[`docs/test_sheets/m5_product.md`](docs/test_sheets/m5_product.md).

The M1 heartbeat lines (`[hb] ...`, for `tools/check_heartbeat.py`) are off by
default; `hb on` turns them back on.

### Console commands (M2)

Type into `pio device monitor`. `help` lists everything; any key stops a stream.

| Command | Does |
|---|---|
| `i2c scan` / `i2c stats` | devices answering on both I2C buses / error counters |
| `imu`, `imu cal`, `imu stream [n] [ms]` | ICM-20608-G reading, gyro-bias calibration, CSV stream (default 100 Hz) |
| `als`, `als stream [n] [ms]` | AP3216C light (lux), proximity, IR |
| `env`, `env stream [n] [ms]` | AHT temperature / humidity, detected chip variant |
| `diag sensors` | one reading from every sensor + bus counters |
| `led`, `led red\|blue on\|off`, `led auto` | indicator LED D4 state / drive it by hand / back to the 1 Hz blink |
| `buzzer chirp\|alarm\|error`, `buzzer beep [ms]` | buzzer patterns (software timer, non-blocking) |
| `keys [s]` | debounced arrow-key events as CSV, with hold time, long-press flag and bounce count |
| `diag mem` | heap, per-task stack high-water mark, CPU share |
| `version`, `uptime`, `hb on\|off`, `reset` | housekeeping |

M3–M5 add: `capture`, `capture on|off|label|start|stop|dump|wipe` (recording
trials, see [`m3_capture.md`](docs/test_sheets/m3_capture.md)); `mode`,
`infer`, `ui`, `selftest model`, `fault`, `climate`, `log`, `wifi on|off`,
`alarm test` (see [`m5_product.md`](docs/test_sheets/m5_product.md)). Every key
press prints `[key] ... press ... edge_to_feedback_ms=` and every release the
action it triggered (`action=arm`, `edge_to_action_ms=`), for the M6 timing tests.

Analyse captured streams with `python tools/m2_sensor_stats.py <log>`. The
M2 capture procedures are in [`docs/test_sheets/m2_sensors.md`](docs/test_sheets/m2_sensors.md)
and [`docs/test_sheets/m2_io.md`](docs/test_sheets/m2_io.md).

## Milestones

| | Milestone | Status |
|---|---|---|
| M0 | Repo + design docs | done (concept sign-off: TODO) |
| M1 | PlatformIO board + FreeRTOS bring-up | done: on-board log passes `check_heartbeat.py` ([results](docs/task_table.md#m1-on-board-results)); LED blink by eye: TODO(measure) |
| M2 | Sensor/module drivers + DIAG mode | all modules verified with evidence logs ([results](docs/task_table.md#m2-on-board-results-i2c-sensors)) except Wi-Fi: capture pending (`capture_m2.py wifi`) |
| M3 | Data capture | firmware (CAPTURE mode → SPI flash log), `tools/capture.py`, protocol ([data/README.md](data/README.md), [test sheet](docs/test_sheets/m3_capture.md)) done; **recording sessions: TODO (H)** |
| M4 | Features, baseline, model, C export | `ml/` pipeline, C features + MLP + baseline, `selftest model` done and smoke-tested on synthetic data; **real training: after M3** |
| M5 | On-device inference + product firmware | firmware done and running (placeholder model); parity + functional check after M4 ([test sheet](docs/test_sheets/m5_product.md)) |
| M6 | Testing and evaluation | [test plan](docs/test_plan.md) T1–T9, guided capture plans (`capture_m2.py m6_*`), analysis scripts (`t1_results.py`, `t7_keys.py`, `size_report.py`, `analyze_soak.py`, `latency_stats.py`) and the [operator sheet](docs/test_sheets/m6_tests.md) done; **runs: after M5 parity (H)** |
| M7 | Report, README, demo | [report draft](docs/report/report.md) (results `TODO(measure)`), [demo shot list](docs/report/demo_script.md), troubleshooting table; **video, contributions, review: H** |

## Results

All figures are `TODO(measure)` until they are produced from real logs in `evidence/`.

| Metric | Value |
|---|---|
| Held-out macro-F1 (model / baseline) | TODO(measure) |
| Inference time median / p95 | TODO(measure) |
| Flash / RAM use (M1 firmware) | FLASH 23104 B (2.20 %), RAM 49712 B (37.93 %, incl. 40 KB RTOS heap), CCM 2192 B, from `evidence/m1_build.txt` |
| FreeRTOS tick vs PC clock (M1) | 999.93 ticks/s (−0.007 %) over 190 s, from `evidence/m1_boot.log` |

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `pio` not recognised | Use the PlatformIO Core CLI terminal, or the full path in *Getting started* |
| Linker: `LAB: No such file or directory` | A path with spaces passed through `build_flags`. Link-stage flags that contain paths go in `tools/pio_fpu_link.py` (SCons quotes them there) |
| Upload: ST-LINK not found | Install the ST-LINK USB driver (STSW-LINK009), use the USB port marked ST-LINK, try another cable |
| Garbage characters on the console | Wrong baud (must be 115200), or the clock is wrong: check the `clock source` line and run `tools/check_heartbeat.py` |
| `!!! PANIC`, `!!! FAULT` or `assert` on the console, red LED blinking fast | The line says what failed. For a FAULT, look up `pc=` with `arm-none-eabi-addr2line -e .pio/build/rt_spark/firmware.elf <pc>` |
| Banner says `FALLBACK: HSE did not start` | The 8 MHz crystal X2 did not start. The firmware still runs at 168 MHz from the internal HSI (less accurate). Report it |

| `!!! PANIC: stack overflow in task <name>` | That task's stack is too small for what it now does. Raise its `*_STACK_WORDS` in `include/app_config.h` (stacks live in the 64 KB CCM RAM, see the build's CCMRAM line) and check the margin with `diag mem` (`hwm_words`) |
| `!!! PANIC: FreeRTOS heap exhausted` | `pvPortMalloc` failed. Tasks and their stacks are static; the heap holds queues, mutexes and timers. Check `diag mem` (`heap_min`) and raise `configTOTAL_HEAP_SIZE` in `include/FreeRTOSConfig.h` |
| `!!! FAULT: UsageFault` at the first floating-point operation (`cfsr` bit 19 `NOCP` set) | The FPU is not enabled or soft-float objects were linked in. The hard-float flags must reach the linker too (`tools/pio_fpu_link.py`); do a `pio run -t clean` after changing flags |
| `[wdg] ... check-in missing, IWDG no longer fed: <task>`, then a reset | That task was blocked for more than the 2 s check-in window. `fault` shows how long ago each task last checked in; after the reset `[wdg] the last reset was the watchdog` names the task and the event log has a `WDG` record |
| LCD shows `Sensor fault - IMU` (or `ALS/PS`, `AHT`) | `fault` shows the failure counters and whether a fault was injected (`fault clear all`); `i2c scan` must list `0x68`, `0x1E` (I2C2) and `0x38` (soft I2C). The sensor is re-initialised every 5 s by itself |
| LCD shows `NO MODEL` | The firmware was built with the placeholder model. Train and export (`ml/`, see `data/README.md`), then rebuild |
| `sd`: `init NOT_PRESENT` | No microSD card, or not seated (this kit has none: `docs/hardware/exceptions.md`). The product logs to the SPI flash instead, so nothing else is affected |
| `capture_m2.py` / `capture.py`: `Cannot open COM3` | Another program holds the port (`pio device monitor`, a VS Code serial monitor, a second capture). Close it and run again |
| Capture tools warn `-dirty` | The firmware was built from uncommitted changes. Commit, then run the tool with `--flash` so the evidence names an exact revision |

## Authors and contributions

BCA182 Embedded Systems Programming, Lab Activity No. 4, MSU-IIT CCS (group submission).

| Member | GitHub | Contributions |
|---|---|---|
| Sherie Grace L. Lupoz | [@Rie07](https://github.com/Rie07) | TODO: fill in |
| Renato G. Beroy | [@Atoyyyyyy](https://github.com/Atoyyyyyy) | TODO: fill in |
| Binyusoph P. Sansarona | [@BinSan21](https://github.com/BinSan21) | TODO: fill in |

Every member must be able to explain the firmware, the model, the hardware roles
and the results (PRD, milestone M7). The detailed record of who changed what is the
[commit history](https://github.com/Atoyyyyyy/bca182-lab4-packsense/commits/main).

## License and attribution

Own code: MIT ([`LICENSE`](LICENSE)). Third-party components and hardware
document sources: [`NOTICE.md`](NOTICE.md).

PackSense is an experimental student prototype. It is not a certified security,
anti-theft or impact-protection device.
