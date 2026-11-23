# PackSense: edge-AI smart backpack insert (RT-Spark / STM32F407)

PackSense sits in a backpack's laptop compartment. It classifies what is happening
to the bag (**STILL · CARRIED · ROUGH · ACCESS**) with a small neural network
running **on the STM32F407**, warns about heat/humidity, and alerts the owner
locally through an LCD, LEDs, an LED matrix and a buzzer. It needs no phone and
no Internet.

BCA182 Embedded Systems Programming, Lab Activity No. 4, MSU-IIT CCS.
Team: Sherie Grace L. Lupoz · Renato G. Beroy · Binyusoph P. Sansarona
([authors and contributions](#authors-and-contributions)).

> **Status:** Milestone M1 (build system + FreeRTOS bring-up): firmware builds,
> waiting for the first on-board boot log. See [Milestones](#milestones).

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
src/          main, drivers/, dsp/, model/, app/               (M1–M5)
test/         native parity tests, golden vectors              (M4)
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

### What a good boot looks like (M1)

The board prints a banner (git revision, reset cause, clock source, PLL
settings read back from RCC, bus clocks, HAL/FreeRTOS versions), then
`[rtos] scheduler started`, then one `[hb] ...` line per second. The red LED
blinks at 1 Hz (500 ms on, 500 ms off).

Check a captured log with:

```bash
python tools/check_heartbeat.py evidence/m1_boot.log
```

## Milestones

| | Milestone | Status |
|---|---|---|
| M0 | Repo + design docs | done (concept sign-off: TODO) |
| M1 | PlatformIO board + FreeRTOS bring-up | in progress: builds; on-board boot log pending |
| M2 | Sensor/module drivers + DIAG mode | — |
| M3 | Data capture | — |
| M4 | Features, baseline, model, C export | — |
| M5 | On-device inference + product firmware | — |
| M6 | Testing and evaluation | — |
| M7 | Report, README, demo | — |

## Results

All figures are `TODO(measure)` until they are produced from real logs in `evidence/`.

| Metric | Value |
|---|---|
| Held-out macro-F1 (model / baseline) | TODO(measure) |
| Inference time median / p95 | TODO(measure) |
| Flash / RAM use | TODO(measure) (from `evidence/m1_build.txt` once captured) |

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `pio` not recognised | Use the PlatformIO Core CLI terminal, or the full path in *Getting started* |
| Linker: `LAB: No such file or directory` | A path with spaces passed through `build_flags`. Link-stage flags that contain paths go in `tools/pio_fpu_link.py` (SCons quotes them there) |
| Upload: ST-LINK not found | Install the ST-LINK USB driver (STSW-LINK009), use the USB port marked ST-LINK, try another cable |
| Garbage characters on the console | Wrong baud (must be 115200), or the clock is wrong: check the `clock source` line and run `tools/check_heartbeat.py` |
| `!!! PANIC`, `!!! FAULT` or `assert` on the console, red LED blinking fast | The line says what failed. For a FAULT, look up `pc=` with `arm-none-eabi-addr2line -e .pio/build/rt_spark/firmware.elf <pc>` |
| Banner says `FALLBACK: HSE did not start` | The 8 MHz crystal X2 did not start. The firmware still runs at 168 MHz from the internal HSI (less accurate). Report it |

More entries are added as issues come up (heap/stack overflow, SD mount, …).

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
