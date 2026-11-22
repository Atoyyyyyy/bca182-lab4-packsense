# PackSense: edge-AI smart backpack insert (RT-Spark / STM32F407)

PackSense sits in a backpack's laptop compartment. It classifies what is happening
to the bag (**STILL · CARRIED · ROUGH · ACCESS**) with a small neural network
running **on the STM32F407**, warns about heat/humidity, and alerts the owner
locally through an LCD, LEDs, an LED matrix and a buzzer. It needs no phone and
no Internet.

BCA182 Embedded Systems Programming, Lab Activity No. 4, MSU-IIT CCS.
Team: Sherie Grace L. Lupoz · Renato G. Beroy · Binyusoph P. Sansarona.

> **Status:** Milestone M0 (repo + design docs). No firmware yet. See
> [Milestones](#milestones).

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

```bash
# 1. Get the hardware documents (not redistributed in git)
python tools/fetch_hw_docs.py

# 2. Build / flash: available from M1
pio run
pio run -t upload
pio device monitor -b 115200
```

## Milestones

| | Milestone | Status |
|---|---|---|
| M0 | Repo + design docs | in progress |
| M1 | PlatformIO board + FreeRTOS bring-up | — |
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
| Flash / RAM use | TODO(measure) |

## Troubleshooting

To be written as issues are met during bring-up (ST-LINK not found, FPU
hard-fault, heap/stack overflow hooks, SD mount failure, …).

## License and attribution

Own code: MIT ([`LICENSE`](LICENSE)). Third-party components and hardware
document sources: [`NOTICE.md`](NOTICE.md).

PackSense is an experimental student prototype. It is not a certified security,
anti-theft or impact-protection device.
