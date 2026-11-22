# PRD — PackSense: Edge-AI Smart Backpack Insert on RT-Spark

> **Audience:** the coding agent working in the project's GitHub repository, plus the human student(s) who own the hardware.
> **Course:** BCA182 Embedded Systems Programming — Laboratory Activity No. 4, "Design and Development of an Edge AI Consumer Product" (100 pts), MSU-IIT CCS.
> **Owner:** Sherie Grace L. Lupoz (BSCA-4) — add teammates here if the instructor allows group work.
> **Team (confirmed 2026-11-22):** Sherie Grace L. Lupoz, Renato G. Beroy, Binyusoph P. Sansarona (group submission).
> **Status:** Draft v1 — the product concept in §2 is a proposal. If the team picks a different concept, rewrite §2–§3 and §7.2 first; everything else stays the same.
>
> **Errata from the schematic review (2026-11-22, see `docs/hardware/pinmap.md`):** RT-Spark HW1.1 schematic labels the temp/humidity sensor as **AHT21** (U8, 0x38, on a *software* I2C bus PE0/PE1), not AHT10. The LCD is a 1.3" 240×240 **ST7789V3** on **FSMC 8-bit**. The board also has an **ES8388 audio codec** (I2C 0x10 + I2S3, onboard microphone + headphone jack) that was missing from §4. Where §4/§6.2 below disagree with the schematic, the schematic wins.

---

## 0. How the agent must use this document

1. **Read the whole PRD before writing any code.** Work through the milestones in §11 **in order**. The lab requires this order: sensors must be verified before training, and the model must fit on the MCU before the UI is finished.
2. **The agent cannot touch the hardware.** Flashing, serial logging, data collection, timing measurements, the demo video, and checking the inventory against the physical board are **human tasks (H)**. The agent writes the code, scripts, docs and checklists for them (**A**). Every task in §11 is tagged A, H, or A→H (agent prepares, human runs and returns logs).
3. **Never invent measurements.** No accuracy, latency, memory, stack high-water mark, pin assignment or test result may appear in the report or README unless a script produced it from a real log in `evidence/` or `data/`. Until then, write `TODO(measure): …`. Fabricated results are an academic-integrity violation under the lab rules.
4. **Never guess pins or hardware facts.** Every pin, bus address, clock and peripheral assignment must come from the RT-Spark schematic or datasheets the human provides (put them in `docs/hardware/`). If a fact isn't in a provided document, stop and ask the human. Do not use a generic STM32F407 board's defaults (the lab explicitly forbids this).
5. **Stick to the mandatory stack:** PlatformIO (`platform = ststm32`, `framework = stm32cube`), STM32Cube HAL/LL, and FreeRTOS. No Arduino, no RT-Thread OS, no Zephyr, and no flashing firmware built for a different MCU.
6. **Commit small and often**, with meaningful messages (`feat(imu): …`, `fix(rtos): …`, `docs: …`, `data: add session S07`). Commit history is graded.
7. **Secrets and personal data never enter git:** no Wi-Fi passwords, API keys, or participant names. Use `include/secrets.h` (git-ignored) with a committed `secrets.example.h`.

---

## 1. Background and constraints from the lab handout

| Constraint | Source requirement |
|---|---|
| Board | RT-Thread RT-Spark (Spark-1), STM32F407ZGT6: 1 MB Flash, 192 KB RAM (128 KB SRAM + 64 KB CCM; **CCM is not DMA-accessible**) |
| Toolchain | VS Code + PlatformIO, `platform = ststm32`, `framework = stm32cube`, a verified custom RT-Spark board manifest, ST-LINK upload/debug |
| RTOS | FreeRTOS added and configured explicitly. Tick, HAL timebase, NVIC priorities and heap/stacks must be consistent, with no duplicated startup/IRQ/timebase definitions |
| Inference | A **trained** model runs **on the STM32F407**. Must work with no Internet. A threshold system is allowed only as a baseline or fallback |
| Sensors | Every physically present, working sensor/module is integrated, either in the product function or in the diagnostic/service mode. The primary app uses **≥2 sensing modalities**, **≥1 user input**, **≥1 local output** |
| Data | Collected on the actual board. Classification: **≥30 train + ≥10 held-out test windows per class**, separate sessions, no leakage between near-duplicate windows |
| RTOS architecture | **≥3 independently scheduled tasks**, plus queues/semaphores/mutexes/event groups, `vTaskDelayUntil` or timer-based sampling, and a task table |
| Testing | ≥20 extra independent trials per class (not used for training), ≥50 timed inferences (median, p95, sense-to-response), 15-min soak, ≥5-min offline run, a controlled failure case, and privacy/safety/accessibility checks |
| Repo | Public GitHub repo with a reproducible build (not just a binary), docs, license/attribution, troubleshooting notes |

---

## 2. Product concept

### 2.1 Problem statement (one paragraph, for the report)

Students and commuters carry laptops, tablets and power banks in backpacks every day. They have no feedback when the bag is handled roughly (dropped, thrown onto a jeepney seat, swung), when a sleeping laptop overheats or traps humidity inside a closed bag, or when someone opens the bag while it is unattended (on a library chair or a classroom hook). **PackSense** is a small insert that sits in the laptop compartment. It senses motion, light/proximity and the bag's internal climate, classifies what is happening to the bag **on the device**, and alerts the owner locally with the LCD, LED and buzzer. It keeps a timestamped event log the owner can review later. It works fully offline. Wi-Fi is only an optional extra for event reporting.

### 2.2 Alternatives considered (report requirement: ≥2)

| Alternative | Why not chosen |
|---|---|
| Context-aware study desk (classifies studying / phone-fidgeting / away) | Studying vs. fidgeting is hard to label objectively and hard to separate with IMU data from a stationary desk. Risk of a weak model |
| Smart medicine/storage box (opened / taken / idle) | Only about 2 natural classes, and it mostly reduces to a light threshold, so ML adds little value over the baseline |
| **PackSense backpack insert (chosen)** | Has a clear user. The classes are physically distinct and safe to reproduce. It uses IMU + light/proximity + temp/humidity together, so the ML has a fair chance to beat a threshold baseline |

### 2.3 Classification states (≥3 required)

| Label | ID | Definition (for labeling) | How to reproduce safely |
|---|---|---|---|
| `STILL` | 0 | Bag at rest on a floor, desk or chair, closed, untouched | Leave the bag on a surface for the window duration |
| `CARRIED` | 1 | Bag worn on the back or held by the handle while the carrier walks normally | Walk a hallway/corridor at normal pace, bag worn properly |
| `ROUGH` | 2 | Abrupt handling: bag set down hard, swung, shaken, tossed onto a soft seat | Set down firmly or toss onto a cushion/bed from ≤50 cm. **No real drops of a bag containing a laptop.** Use a dummy load |
| `ACCESS` | 3 | Bag opened and hand inserted or contents rummaged (zipper motion + light change + proximity) | Open the compartment, reach in, move items, close |

ML inputs: IMU (primary) + AP3216C ambient light/proximity (primary). AHT10 is **context only**: it drives a rule-based "bag climate" warning and is not a classifier feature, per the lab's advice to avoid irrelevant features.

**Product behavior:**
- `ROUGH` → buzzer chirp + amber LED + LCD "Rough handling detected", logged
- `ACCESS` while **Guard mode** is armed → alarm pattern + red LED. Silenced by button acknowledge.
- `ACCESS` while unarmed → log only
- AHT10 temperature above a threshold (e.g. > 40 °C; final value set from measurements) or RH > 80 % for N minutes → "Bag too hot/humid, check laptop" warning
- LCD home screen shows the current state, its confidence, the sensor values behind it ("why"), the guard state and the climate

### 2.4 Target user, environment, goal

- **User:** university students/commuters carrying electronics in backpacks.
- **Environment:** inside a backpack. Indoor/outdoor, 20–45 °C, dark when closed, irregular motion.
- **Goal:** give the owner timely local awareness of rough handling, unattended access and heat/humidity risk, without a phone or network.

---

## 3. Acceptance criteria (product level)

| ID | Criterion | Measurement |
|---|---|---|
| AC-1 | Held-out macro-F1 ≥ 0.85, and no class recall < 0.75 (targets; report the real numbers honestly even if missed) | `ml/evaluate.py` on held-out sessions |
| AC-2 | Trained model beats the threshold baseline on macro-F1 | Same split, same windows |
| AC-3 | On-device prediction matches the host model on ≥ 98 % of the golden test vectors (float) or argmax match ≥ 98 % (quantized) | Parity test, §8.6 |
| AC-4 | Median inference time and p95 reported from ≥ 50 runs. Sense-to-feedback latency < 500 ms after window close | DWT cycle counter logs |
| AC-5 | Button press → visible/audible response < 150 ms | GPIO toggle timestamp log |
| AC-6 | 15-min soak with 0 crashes/hangs/watchdog resets. Missed sensor reads logged and counted | Soak log + `tools/analyze_soak.py` |
| AC-7 | ≥ 5 min offline (Wi-Fi off/unplugged) with inference and feedback unchanged | Log + video |
| AC-8 | Controlled failure (sensor unplugged/NACK, SD absent, Wi-Fi loss) → fault shown, invalid data rejected, no crash, other tasks keep running | Test log |
| AC-9 | Flash < 70 % and RAM < 80 %. No task stack high-water mark < 64 words | PlatformIO size report + diag dump |

**Errors that matter:** a missed `ACCESS` while guarded (false negative, worst for the user); a false `ROUGH`/`ACCESS` alarm (false positive, annoying, users disable the device); `CARRIED` mislabeled as `ROUGH` (most likely confusion).

**Risks:**
- Privacy: logs contain only state + time, never audio or images.
- Safety: buzzer volume is moderate. It is an experimental prototype, not a certified security or theft device.
- Accessibility: every alert is shown both audibly and visually (LED + LCD text), large LCD font, and the buzzer can be muted via a button.

---

## 4. Hardware inventory and component-to-function matrix

> **A→H:** The agent creates `docs/hardware/inventory.md` from this template. The human fills in the *Verified?* column from the physical kit and the schematic (`docs/hardware/RT-Spark_schematic.pdf`). Pins and buses below are **placeholders to verify**. Do not code against them until the schematic confirms them.

| Component | Bus / pins (VERIFY from schematic) | Verified? | Role | Used in |
|---|---|---|---|---|
| ICM20608 6-axis IMU | I2C (addr 0x68/0x69 per AD0) | ☐ | **Primary sensing** | ML features, diag |
| AP3216C ALS + proximity (+IR) | I2C (addr 0x1E) | ☐ | **Primary sensing** | ML features, diag |
| AHT10 temp/humidity | I2C (addr 0x38) | ☐ | **Context sensing** | climate rule, diag |
| LCD (expected ~240×240, controller and interface TBD: FSMC vs SPI) | ? | ☐ | **User interaction / output** | home, alerts, diag |
| RGB / indicator LED(s) | GPIO | ☐ | **Output** | state color |
| LED matrix (if fitted) | ? | ☐ | **Output / diag** | alert animation or self-test |
| Buzzer | GPIO / TIM PWM | ☐ | **Output** | alerts |
| Push buttons (KEY0..N, WK_UP) | GPIO + EXTI | ☐ | **User input** | ack, arm guard, mode, capture labeling |
| RTC (+ battery?) | internal RTC, LSE | ☐ | **Logging** | timestamps |
| microSD/TF | SDIO | ☐ | **Logging** | event log + data capture |
| External SPI flash | SPI | ☐ | **Logging / config** | settings, fallback log |
| Wi-Fi module (e.g. RW007, if fitted) | SPI/UART | ☐ | **Communication (optional)** | event upload, status in diag |
| USB-UART / ST-LINK VCP | USART | ☐ | **Diag / data capture** | console, CSV stream |
| IR TX/RX | ? | ☐ | **Diag** (if fitted) | loopback self-test |
| CAN / RS485 / expansion | — | ☐ | Only if a module is attached; otherwise document as "connector only, not a sensor" | — |

Anything absent or broken → record it in `docs/hardware/exceptions.md` with the reason and a slot for the instructor's verification signature.

---

## 5. System architecture

```
 SENSE                       PROCESS                    INFER (on STM32)          ACT                       RECORD
 ICM20608 @100 Hz ─┐                                                              LCD state + "why"         SD log (CSV)
 AP3216C  @20 Hz  ─┼─► SensorTask ─► ring buffer ─► window 2 s / hop 0.5 s ─►    LED color                 ext-flash cfg
 AHT10    @1 Hz   ─┘    validate, timestamp        features (DSP)    ─► MLP ─►    Buzzer pattern       ─►   optional Wi-Fi
 Buttons (EXTI) ─────────────────────────────────────────────────────► decision ◄─ ack / arm / mode        event report
                                                                      + smoothing
```

### 5.1 Operating modes (state machine in `app_mode.c`)

| Mode | Entered by | Behavior |
|---|---|---|
| `BOOT_SELFTEST` | power-on | Init every peripheral, record pass/fail per component, show summary 2 s |
| `RUN` (default) | after self-test | Normal product: classify, alert, log |
| `GUARD` | long-press KEY_A in RUN | Same as RUN, but `ACCESS` triggers the alarm |
| `CAPTURE` | KEY combo at boot or a console command | Streams raw timestamped samples as CSV over UART (and to SD) with the current label/session. Buttons cycle the label and start/stop a trial |
| `DIAG` (service) | KEY_MODE | Pages: live readings of every sensor with units + error counts; LED/buzzer/LED-matrix self-test; RTC time; SD/flash read-write test; Wi-Fi status; task table (stack HWM, heap free, CPU %); inference stats |

This makes every verified module reachable from the single firmware, as the lab requires.

---

## 6. Firmware specification

### 6.1 Project/PlatformIO setup (Milestone M1)

- `boards/rt_spark.json`: custom manifest. `mcu: stm32f407zgt6`, `f_cpu: 168000000L`, `cpu: cortex-m4`, Flash 1 MB, RAM 128 KB (CCM handled in the linker script), `upload.protocol: stlink`, `debug.tools.stlink`. HSE frequency comes from the schematic (build flag `HSE_VALUE`).
- `platformio.ini` (target shape; adjust after verification):

```ini
[env:rt_spark]
platform = ststm32
framework = stm32cube
board = rt_spark
board_build.ldscript = linker/STM32F407ZGTx_FLASH.ld   ; with .ccmram section
upload_protocol = stlink
debug_tool = stlink
monitor_speed = 115200
build_flags =
  -DSTM32F407xx -DUSE_HAL_DRIVER -DHSE_VALUE=8000000U  ; VERIFY HSE
  -mfpu=fpv4-sp-d16 -mfloat-abi=hard
  -Iinclude -Ilib/FreeRTOS/include -Ilib/FreeRTOS/portable/GCC/ARM_CM4F
  -Wall -Wextra
build_unflags = -Os
extra_scripts = pre:tools/pio_fpu_link.py   ; pass FPU flags to the linker as well
```

- **FreeRTOS integration:** vendor the FreeRTOS-Kernel (pin and record the version) in `lib/FreeRTOS/` with only `portable/GCC/ARM_CM4F` and `portable/MemMang/heap_4.c`.
  - `FreeRTOSConfig.h`: `configCPU_CLOCK_HZ = SystemCoreClock`, `configTICK_RATE_HZ 1000`, `configPRIO_BITS 4`, `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5`, `configCHECK_FOR_STACK_OVERFLOW 2`, `configUSE_MALLOC_FAILED_HOOK 1`, `configUSE_TRACE_FACILITY 1`, `configGENERATE_RUN_TIME_STATS 1` (DWT/TIM based), `INCLUDE_uxTaskGetStackHighWaterMark 1`, `configTOTAL_HEAP_SIZE` sized from measurement (start ~40 KB).
  - Map `vPortSVCHandler → SVC_Handler`, `xPortPendSVHandler → PendSV_Handler`, `xPortSysTickHandler → SysTick_Handler`. **Remove** the duplicate HAL/Cube definitions of these from `stm32f4xx_it.c`.
  - HAL timebase moves to **TIM6** (`stm32f4xx_hal_timebase_tim.c`, `HAL_InitTick` override), so SysTick belongs to FreeRTOS only.
  - `HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4)`. Every ISR that calls `…FromISR` APIs has priority number ≥ 5.
- **Exit criterion M1:** clean build. The human flashes it and captures a serial log showing the clock config, HAL init and "scheduler started", plus a heartbeat task blinking an LED. The log is saved to `evidence/m1_boot.log`.

### 6.2 Drivers (`src/drivers/`, HAL/LL only, one `.c/.h` per device)

Common rules: each driver exposes `init()`, `read()` → `status_t` (OK / NACK / TIMEOUT / RANGE_ERR / NOT_PRESENT), with unit-converted output and an error counter. I2C access goes through an `i2c_bus` wrapper guarded by a **mutex** (all three sensors may share a bus). Read the WHO_AM_I or ID register at init to detect presence.

| Driver | Key points |
|---|---|
| `icm20608.c` | WHO_AM_I check; ±4 g, ±500 dps; DLPF ~44 Hz; ODR 100 Hz (use data-ready INT if wired, else timer-paced burst read of 14 bytes); output m/s² and dps; boot-time gyro bias calibration (bag still, 2 s) |
| `ap3216c.c` | Reset, ALS+PS+IR mode; read ALS (lux, per range), PS (raw, with IR-overflow flag → invalid) |
| `aht10.c` | Init/calibrate cmd, trigger measurement, wait ≥ 75 ms non-blocking (`vTaskDelay`), check busy/cal bits, convert to °C / %RH |
| `lcd.c` + `ui.c` | Per the verified controller/interface. Minimal font renderer, partial redraws only (no full-screen refresh each frame) |
| `led.c`, `buzzer.c` | RGB via GPIO or TIM PWM; buzzer patterns (chirp / alarm / error) driven by a software timer, non-blocking |
| `buttons.c` | EXTI → ISR posts to a queue with timestamp; debounce 30 ms in the task; short/long-press detection |
| `rtc.c` | LSE if present (verify), set time from a console command, provide `ts_ms()` + calendar time |
| `sdlog.c` | FatFs on SDIO (vendored FatFs, record version); append-only CSV; graceful `NOT_PRESENT` |
| `extflash.c` | JEDEC ID read; config block (thresholds, mute flag) with CRC |
| `wifi.c` | Optional. Status + non-blocking event POST with timeout. Never on the inference path |

### 6.3 FreeRTOS task table (fill measured columns later)

| Task | Function | Prio | Stack (words) | Period / activation | Communication | HWM (measured) |
|---|---|---|---|---|---|---|
| `SensorTask` | Read IMU @100 Hz; ALS/PS @20 Hz (every 5th tick); validate, timestamp, push samples | 5 (highest app) | 512 | `vTaskDelayUntil` 10 ms | → `qSamples` (queue) ; I2C mutex | TODO |
| `EnvTask` | AHT10 @1 Hz, climate rule | 2 | 384 | `vTaskDelayUntil` 1000 ms | I2C mutex; → `stateMutex`-protected `g_env` | TODO |
| `InferTask` | Window buffer, features, MLP, smoothing, decision | 4 | 1024 | Event: notification every 50 new samples (0.5 s hop) | ← `qSamples`; → `qEvents` | TODO |
| `UiTask` | LCD/LED/buzzer, button handling, mode switching | 3 | 768 | 50 ms refresh + `qButtons` / `qEvents` | ← `qButtons`, `qEvents`; event group `evMode` | TODO |
| `LogTask` | SD/flash logging, optional Wi-Fi report, CSV stream in CAPTURE | 1 | 1024 | Blocks on `qLog` | ← `qLog`; `sdMutex` | TODO |
| `DiagTask` (or part of UiTask) | Runtime stats, HWM, heap, error counters | 1 | 512 | 1000 ms when DIAG is active | reads stats | TODO |
| Idle / Timer svc | FreeRTOS | 0 / 2 | default | — | software timers for buzzer patterns | TODO |

Rules: no busy-waits except µs-level HAL ops; no `HAL_Delay` in tasks; no `printf` from ISRs; one task owns each peripheral or the peripheral is mutex-protected; an independent watchdog (IWDG) is fed only when every task has checked in (a check-in bitmask in an event group).

### 6.4 Sensor validation and fault handling

- Reject a sample if the read status ≠ OK, the value is outside the physical range (|a| > 4 g saturated for 3+ consecutive samples, T outside −20..85 °C, RH outside 0..100), the PS IR-overflow flag is set, or a timestamp gap is > 3× the period.
- A window with > 5 % invalid samples is **not classified**. The UI shows "Sensor fault – <name>", the log records it, and the decision falls back to the last valid state marked `UNCERTAIN`.
- After 3 consecutive failures the driver goes to `DEGRADED` and re-inits every 5 s without blocking other tasks.
- Every fault is counted and shown in DIAG.

---

## 7. Data collection and ML pipeline

### 7.1 Capture protocol (A writes firmware + scripts; H runs it)

- **Signals:** ax, ay, az (m/s²), gx, gy, gz (dps) @ 100 Hz; als (lux), ps (raw) @ 20 Hz, held/forward-filled to 100 Hz in post-processing; T/RH @ 1 Hz (context only, logged).
- **CSV schema** (UART line and SD file): `session_id,trial_id,label,t_ms,ax,ay,az,gx,gy,gz,als,ps,ps_valid,temp,rh,flags`
- **Host script** `tools/capture.py`: opens the serial port, writes `data/raw/<session_id>.csv`, prompts the operator for the label and trial start/stop (or mirrors the board's button labeling), and appends to `data/manifest.csv` (session, date, operator code (not a name), bag type, load, location, label, n_trials, notes).
- **Variation to cover:** ≥ 2 bags, ≥ 2 carriers (anonymized codes P1, P2…), empty vs. loaded with a dummy weight, indoor bright vs. dim rooms, different set-down surfaces.
- **Volume:** for each class, sessions are planned so that after windowing there are **≥ 30 train windows from training sessions and ≥ 10 test windows from different sessions**. Aim for 2× that. A further **≥ 20 independent trials per class** are collected later for final system testing (§9) and are never used in training or tuning.

### 7.2 Windowing and features

- Window **2.0 s (200 samples)**, hop 0.5 s, overlap allowed **only within a split**. **Split by session** (GroupKFold / explicit session lists in `ml/split.yaml`), so no trial contributes windows to both train and test.
- Features (~30–40, all computable in C on the MCU, same code path as Python):
  - Per axis + magnitude |a|, |g|: mean, std, min, max, range, RMS, mean-crossing rate
  - |a| jerk peak (max |Δ|a||), count of |a| peaks > 1.5 g
  - Low-band spectral energy of |a| (e.g. 0.5–3 Hz gait band vs. > 5 Hz), using a 256-pt FFT (CMSIS-DSP `arm_rfft_fast_f32` if available in the framework; otherwise a small self-written radix-2 FFT, mirrored in Python)
  - Gravity-vector tilt change between window start and end
  - ALS: mean, Δ(max−min), log-lux slope; PS: max, fraction of samples > near-threshold
- Normalization: z-score with train-set mean/std exported as constants.

### 7.3 Models

- **Baseline (required):** a hand-tuned rule set in `ml/baseline.py` **and** `src/app/baseline.c`. Example: std|a| < τ1 → STILL; jerk/peak > τ2 → ROUGH; ALS jump + PS near → ACCESS; else CARRIED. τ tuned on train only.
- **Trained model:** small MLP in scikit-learn or Keras, input ~36 → 24 → 16 → 4, ReLU, softmax. Compare with a shallow decision tree / random forest if time allows. Pick the smallest model meeting AC-1.
- **Export:** `ml/export_c.py` generates `src/model/model_weights.h` (float32 first; int8 quantization optional) + `model_params.h` (feature order, mean/std, class names, model version hash). Inference is a ~100-line pure-C `mlp_infer()` in `src/model/`, so no heavyweight runtime is needed. (Edge Impulse / TFLM are acceptable alternatives; if used, record versions and license, and check the footprint.)
- **Smoothing / decision:** majority vote of the last 3 windows, or an EMA of probabilities. Alert only if p ≥ 0.7. `ROUGH` uses an event-style rule (any single window ≥ 0.8) because it is brief.
- **Outputs of `ml/evaluate.py`** (into `ml/reports/`): confusion matrix PNG + CSV, per-class precision/recall/F1, macro-F1, baseline vs. model table, misclassified-window list with session/trial IDs for the error analysis, class counts (imbalance).

### 7.4 Host–device parity (golden vectors)

`ml/export_c.py` also writes `test/golden/golden_vectors.h`: 50 raw test windows + expected features + expected probabilities. The firmware has a console command `selftest model`, which recomputes features and inference on the MCU and prints max abs error and argmax match → evidence for AC-3. A `pio test` native env can run the same C feature/inference code on the PC.

---

## 8. Measurement and diagnostics instrumentation (agent implements; human runs)

1. **Inference timing:** DWT `CYCCNT` around `features()` and `mlp_infer()` separately. Log `t_feat_us,t_inf_us` per window. `tools/latency_stats.py` → n, median, p95, max.
2. **Sense-to-response:** timestamp of the window's last sample → timestamp when the LED/buzzer is driven; logged as `lat_ms`.
3. **Button responsiveness:** EXTI timestamp → UI feedback timestamp.
4. **Memory:** save the `pio run -v` size output to `evidence/size.txt`. Console `diag mem` prints `xPortGetFreeHeapSize`, `xPortGetMinimumEverFreeHeapSize`, per-task HWM (`uxTaskGetSystemState`), CPU % from run-time stats.
5. **Soak:** `tools/analyze_soak.py` parses a ≥ 15-min log and counts resets (boot banner count), missed reads, gaps, false alerts (from an operator annotation file), and button failures.
6. **Console commands** (UART, non-blocking line parser in UiTask or LogTask): `help`, `diag`, `diag mem`, `diag sensors`, `rtc set YYYY-MM-DD hh:mm:ss`, `mode run|guard|capture|diag`, `label <id>`, `selftest model`, `selftest io`, `wifi off|on`, `fault inject <sensor>` (simulates a NACK for the controlled-failure demo).

---

## 9. System test plan (`docs/test_plan.md`, written before the final demo)

| Test | Procedure | Pass condition | Evidence |
|---|---|---|---|
| T1 Functional per class | ≥ 20 new independent trials per class, scripted sheet (`docs/test_sheets/T1.csv`) | Observed vs. expected table; per-class accuracy reported | log + sheet |
| T2 Latency | ≥ 50 inferences in RUN | Median/p95 reported; AC-4 | `latency_stats` output |
| T3 Memory | build + `diag mem` after 10 min of running | AC-9 | `size.txt`, diag dump |
| T4 Soak | 15+ min of mixed use | AC-6 | soak log + analysis |
| T5 Offline | Wi-Fi disabled/unplugged ≥ 5 min while classifying | AC-7 | log + video segment |
| T6 Controlled failure | (a) `fault inject imu` or physically disconnect where safe, (b) remove the SD card, (c) Wi-Fi loss/reconnect, (d) reset during run | AC-8, recovers, scheduler responsive | log + video |
| T7 Input control | Ack, arm/disarm, mute, mode switch, each ×10 | 10/10, < 150 ms | log |
| T8 Diagnostics | Walk all DIAG pages; every verified module shows live data/self-test | All ☑ in inventory | video |
| T9 Privacy/safety/accessibility | Review checklist | Documented | report section |

Report at least **two improvements supported by the test data** (e.g. "CARRIED→ROUGH confusion on stairs → add stair sessions", "ALS saturates in sunlight → use range auto-switch").

---

## 10. Repository structure

```
packsense-rtspark/
├─ README.md                 # overview, build/flash steps, usage, troubleshooting, attribution
├─ LICENSE                   # MIT for own code; third-party licenses listed in NOTICE.md
├─ NOTICE.md                 # FreeRTOS (MIT), STM32Cube HAL (BSD-3), FatFs, CMSIS…, with versions
├─ platformio.ini
├─ boards/rt_spark.json
├─ linker/STM32F407ZGTx_FLASH.ld
├─ include/                  # FreeRTOSConfig.h, stm32f4xx_hal_conf.h, app_config.h, secrets.example.h
├─ lib/FreeRTOS/ (vendored, version pinned)   lib/FatFs/
├─ src/
│  ├─ main.c  system_clock.c  stm32f4xx_it.c  hal_timebase_tim6.c  freertos_hooks.c
│  ├─ drivers/   (i2c_bus, icm20608, ap3216c, aht10, lcd, led, buzzer, buttons, rtc, sdlog, extflash, wifi)
│  ├─ dsp/       features.c  fft.c
│  ├─ model/     mlp_infer.c  model_weights.h  model_params.h
│  ├─ app/       tasks.c  app_mode.c  decision.c  baseline.c  ui.c  console.c  diag.c
├─ test/                     # pio test: native feature/inference parity, golden vectors
├─ ml/                       # requirements.txt, prepare.py, features.py, baseline.py, train.py, evaluate.py, export_c.py, split.yaml, reports/
├─ tools/                    # capture.py, latency_stats.py, analyze_soak.py, pio_fpu_link.py
├─ data/                     # manifest.csv, raw/ (anonymized), README (collection procedure)
├─ docs/
│  ├─ hardware/  inventory.md  exceptions.md  pinmap.md  (schematic PDFs if license permits)
│  ├─ design.md  (problem, alternatives, block diagram, component matrix, acceptance criteria)
│  ├─ task_table.md  test_plan.md  test_sheets/  report/  (lab report source)
└─ evidence/                 # build logs, boot logs, diag dumps, test logs, screenshots, video link
```

`ml/features.py` and `src/dsp/features.c` must implement **identical** features in the same order. Any change to one requires the same change to the other plus regenerated golden vectors.

---

## 11. Milestones and work breakdown

Tags: **A** = agent, **H** = human, **A→H** = agent prepares, human executes and returns artifacts.

### M0 — Repo and design docs (Task 1)
- A: scaffold the repo per §10, `.gitignore` (`.pio/`, `secrets.h`, `data/raw/*_private*`), README skeleton, LICENSE/NOTICE.
- A: `docs/design.md` from §2–§3, a block diagram (Mermaid), the component-to-function matrix template.
- H: confirm the concept and states with the team/instructor; drop the schematic + datasheets into `docs/hardware/`.
- **Done when:** the design doc is committed and the instructor has OK'd the concept (if required).

### M1 — Build system + FreeRTOS bring-up (Task 2)
- A: board manifest, linker script with `.ccmram`, clock config (168 MHz from verified HSE), FreeRTOS integration per §6.1, heartbeat task, UART console printing a boot banner.
- A→H: flash, capture `evidence/m1_boot.log`, run `pio run -v > evidence/m1_build.txt`.
- **Done when:** the scheduler runs, the LED blinks at the expected rate, and there are no duplicate handler warnings.

### M2 — Sensor and module bring-up + DIAG mode (Task 2)
- A: drivers in §6.2 one at a time. Each gets its own commit and a DIAG page or console readout with units, timestamps and error states.
- A→H: per module, run it and save `evidence/m2_<module>.log` (+ photo/screenshot of the LCD). Fill in `inventory.md` (Verified ☑ / exception).
- A: measure sampling intervals from logs (`tools/` script), note offsets/calibration (e.g. accel at rest ≈ 9.81 m/s², gyro bias).
- **Done when:** every present module is live in DIAG, and absent/broken ones are documented in `exceptions.md`.

### M3 — Data capture (Task 3)
- A: CAPTURE mode, `tools/capture.py`, `data/README.md` with the protocol from §7.1, manifest template, labeling cheat sheet.
- H: record sessions. Commit anonymized raw CSVs + manifest (`data: add session Sxx`).
- A: `ml/prepare.py` → cleaning report (excluded rows/windows with reasons), window counts per class per split. **Gate:** ≥ 30 train / ≥ 10 test windows per class from separate sessions. If not met, tell the human exactly which classes/sessions are missing.

### M4 — Features, baseline, model, export (Task 3)
- A: `features.py` + `features.c`, baseline, training, evaluation, export, golden vectors, native parity test.
- A: estimate the model footprint (weights bytes, scratch RAM) and confirm it fits next to the RTOS + drivers **before** the UI work (lab ordering requirement).
- **Done when:** `ml/reports/` has real metrics, the baseline comparison and the error analysis.

### M5 — On-device inference + product firmware (Task 4)
- A: the full task set from §6.3, decision/smoothing, UI screens (home with state + confidence + "why" values, alert, guard, climate warning), buttons, logging, optional Wi-Fi with timeouts and a local fallback, IWDG, fault handling.
- A→H: flash; run `selftest model` → `evidence/m5_parity.log`; quick functional check per class.
- **Done when:** the board classifies live with no laptop or network attached.

### M6 — Testing and evaluation (Task 5)
- A: `docs/test_plan.md`, test sheets, analysis scripts.
- H: run T1–T9 and save logs to `evidence/`.
- A: generate the results tables/plots **only from those logs**; fill in the task table HWMs, memory, latency.

### M7 — Report, README, demo
- A: draft the lab report in `docs/report/` with all required sections (problem & design, verified inventory, block diagram, task table, dataset, model & baseline, results, limitations, ≥ 2 data-backed improvements, references, model + compiler versions); a README with build/flash steps and troubleshooting (ST-LINK not found, FPU hard-fault, heap overflow hook, SD mount fail…); a demo video script/shot list covering §12.
- H: record the video, review everything, and make sure every team member can explain the firmware, model, hardware roles and results.

---

## 12. Demo checklist (video or live)

1. Power on → self-test summary on the LCD and console.
2. RUN: show STILL → CARRIED → ROUGH → ACCESS, with the LCD showing the state, confidence and "why".
3. Arm GUARD → open the bag → alarm → button acknowledge.
4. Climate warning (warm the AHT10 gently by hand/breath; no heat guns).
5. DIAG pages: every verified module live, task table with HWMs, heap, inference timing.
6. Wi-Fi off / unplugged for ≥ 5 min → still classifying. Reconnect handling if Wi-Fi is used.
7. Controlled failure (`fault inject` or SD removal) → fault shown, system keeps running.
8. Show the repo: build from clean with `pio run`, then upload.

---

## 13. Rubric traceability

| Rubric criterion (pts) | Covered by |
|---|---|
| Original problem, design, acceptance criteria (10) | §2, §3, `docs/design.md`, M0 |
| Board audit + comprehensive integration (15) | §4, §5.1 DIAG, §6.2, M2 |
| STM32Cube/FreeRTOS architecture & reliability (15) | §6.1, §6.3, §6.4, M1, M5 |
| Data, DSP, model validation (15) | §7, M3, M4 |
| On-device inference + consumer feedback (20) | §7.3–7.4, §5, M5, T5 |
| Testing & evidence-based evaluation (15) | §8, §9, M6 |
| Report + GitHub repo (5) | §10, M7 |
| Live demo + explanation (5) | §12, M7 |

---

## 14. Out of scope / non-goals

- A cloud classifier or phone app (cloud is optional telemetry only).
- Certified anti-theft or impact protection claims. The device is an **experimental prototype**.
- Battery/power optimization beyond basics (may be listed as a future improvement).
- Using non-board sensor data (a phone may only be used to *annotate* trials, e.g. video for labeling).

## 15. Open questions for the human (agent: ask these before M1 if unanswered)

1. Individual or group submission? Team members' names for the docs?
2. Exact board revision and which modules are physically fitted in your kit (Wi-Fi module? LED matrix? SD card? RTC battery?)
3. Which LCD controller/interface, and which pins, per the schematic?
4. Is an instructor sign-off needed for the concept, and for the `ROUGH` simulation method?
5. Python environment available locally (versions), or Edge Impulse preferred?
6. Submission deadline (for milestone planning)?

---

## Appendix A — Kickoff prompt for the coding agent

```
Read PRD.md fully. You are implementing BCA182 Lab Activity 4 (PackSense) on the RT-Spark
(STM32F407ZGT6) with PlatformIO + STM32Cube HAL/LL + FreeRTOS. Follow §0 rules strictly:
no guessed pins (use docs/hardware/ only), no fabricated measurements (TODO(measure) until
logs exist), small meaningful commits. First ask me the open questions in §15 that are still
unanswered, then start Milestone M0. At the end of each milestone, stop, list the human (H)
steps I must perform and the exact evidence files I should return, and wait.
```
