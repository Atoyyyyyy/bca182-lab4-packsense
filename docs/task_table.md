# FreeRTOS task table

The planned application tasks are in PRD §6.3. This table lists what the firmware
**actually creates** at the current milestone. The *HWM* column (stack
high-water mark = smallest free stack ever, in words) is filled in only from
real logs in `evidence/`.

Kernel settings: `include/FreeRTOSConfig.h` (1 kHz tick, 7 priorities, heap_4
40 KB, stack overflow check method 2).

## M1 (bring-up)

| Task | Function | Prio | Stack (words) | Stack location | Period / activation | Communication | HWM (measured) |
|---|---|---|---|---|---|---|---|
| `Heartbeat` | Toggle red LED (PF12), print one status line per second | 1 | 256 | static, CCM RAM | `xTaskDelayUntil` 500 ms | console mutex | **126** words free (130 of 256 used at peak); `hb_hwm` in `evidence/m1_boot.log`, constant over 190 s |
| `IDLE` | FreeRTOS idle | 0 | 128 | kernel-provided static | always ready | — | **101** words free (M2 firmware; `diag mem` in `evidence/m2_imu.log`) |
| `Tmr Svc` | FreeRTOS software timers | 2 | 256 | kernel-provided static | timer command queue | — | **223** words free (M2 firmware; `diag mem` in `evidence/m2_imu.log`) |

## M2 (bring-up, in progress)

Added on top of M1:

| Task | Function | Prio | Stack (words) | Stack location | Period / activation | Communication | HWM (measured) |
|---|---|---|---|---|---|---|---|
| `CLI` | Console line editor + commands; runs the DIAG readouts and sensor streams (`imu stream` paces itself with `xTaskDelayUntil`) | 2 | 1024 | static, CCM RAM | blocks on the console RX stream buffer | ← USART1 RX ISR (stream buffer); I2C bus mutexes; console mutex | **788** words free (236 of 1024 used at peak) after the IMU calibration and streams; `diag mem` in `evidence/m2_imu.log` |

The buzzer patterns run on a FreeRTOS software timer (`Tmr Svc` task), so
`buzzer_play()` never blocks the caller. The `keys` command is the button
queue's consumer for now; the `UiTask` takes that role in M5.

The PRD 6.3 tasks (`SensorTask`, `EnvTask`, …) replace the CLI as the sensor
owners from M3/M5 on; the drivers already lock each bus with a mutex, so
several tasks can share them.

### M2 on-board results: I2C sensors

Captured with `tools/capture_m2.py` and summarised with
`python tools/m2_sensor_stats.py evidence/m2_*.log`. Firmware `8a718b5` (IMU)
and `f404b0e` (light, temperature/humidity), both clean builds. Each log has a
`_cues.txt` file next to it that says what the operator did and when.

| Sensor | Check | Measured | Source |
|---|---|---|---|
| I2C buses | devices present | i2c2: `0x10` ES8388, `0x1E` AP3216C, `0x68` ICM-20608-G; soft: `0x38` AHT | `i2c scan`, `m2_imu.log` |
| I2C buses | errors over the captures | 0 NACKs, 0 timeouts, 0 bus errors, 0 recoveries | `i2c stats` in `m2_als.log`, `m2_env.log` |
| ICM-20608-G | identity | WHO_AM_I `0xAF` | `m2_imu.log` |
| ICM-20608-G | sample rate, board still | 1000/1000 OK, 100.01 Hz, interval 9–10 ms, 0 gaps > 30 ms | stream 1 |
| ICM-20608-G | accel at rest | \|a\| = 10.239 m/s² (sd 0.018), **+0.432 m/s² (+4.4 %) above g0**; ax −0.345, ay 0.182, az 10.231 | stream 1 |
| ICM-20608-G | gyro bias (removed by `imu cal`) | (−0.154, −1.022, −0.410) °/s; after cal: mean ≤ 0.003 °/s, sd ≈ 0.03 °/s | `imu cal`, stream 1 |
| ICM-20608-G | motion | 500/500 OK while tilting; gyro x −228 … +273 °/s; gravity moves between axes | stream 2 |
| AP3216C | room light / covered | ~23 lux → **0.00 lux** with a hand over it; PS 0–7 → **1023** (10-bit full scale), `object=near` | `m2_als.log` |
| AP3216C | torch | not shown: lux stayed 1–6 (the phone shaded the sensor) | `m2_als.log`, cues |
| AP3216C | sample timing | 40/40 OK, exactly 250 ms | `m2_als.log` |
| AHT | variant | AHT2x (status `0x18`, CRC byte valid); chip marking: TODO (H) | `m2_env.log` |
| AHT | room / breath | 31.05 °C, 67.3 % RH → peak **32.51 °C, 74.1 % RH** around the breath cue, then decaying (65.6 % at the end) | `m2_env.log` |
| AHT | sample timing | 30/30 OK, exactly 1000 ms | `m2_env.log` |
| FreeRTOS | heap after the IMU run | free 40944 B, minimum ever 40928 B | `diag mem`, `m2_imu.log` |

**Calibration notes (from these logs, for M3/M4):**

- The accelerometer reads |a| about 4.4 % high at rest, mostly on Z
  (+0.42 m/s², about 43 mg). The ICM-20608-G datasheet allows ±60 mg zero-g
  offset, so this is within spec. The ML features use the *change* in
  acceleration, so a fixed offset does not matter there. If absolute g is
  needed, subtract a per-board offset measured the same way.
- Gyro bias is up to 1 °/s (Y axis). `imu cal` removes it (residual mean
  < 0.01 °/s), so the product firmware calibrates while the bag is still.
- The AHT sits on the board next to other parts, so it can read a little above
  room air. Use it for *trends* (bag heating up, getting humid) rather than
  absolute room temperature.

### M2 on-board results: indicator LED, buzzer, arrow keys

Captured with `tools/capture_m2.py leds | buzzer | keys`, firmware `f8029de`
(clean build). The LED and buzzer checks are the operator's answers recorded
in the `_cues.txt` files; the key results come from the firmware's event lines.

| Part | Check | Measured | Source |
|---|---|---|---|
| LED D4 | red / blue / both / off | all seen at D4 as commanded; `led auto` resumes the 1 Hz heartbeat blink (closes the M1 blink-by-eye check) | `m2_leds.log`, operator: all yes |
| Buzzer | silent at reset | yes | `m2_buzzer_cues.txt` |
| Buzzer | pattern lengths (firmware ticks, start → `done`) | beep 300 ms → 300; chirp 80; error (3 beeps) 420; alarm 3000, stops by itself | `m2_buzzer.log`, operator heard each |
| Keys | label → pin | DOWN cue → DOWN (PC1), LEFT cue → LEFT (PC0), RIGHT cue → RIGHT (PC4), held UP → UP (PC5): printed labels match the pin map | `m2_keys.log` |
| Keys | long press | UP held **2009 ms** → `long=1` (threshold 800 ms) | `m2_keys.log` |
| Keys | debounce | DOWN: 4 raw edges for one press, reported as 1 press + 1 release; short taps 86–212 ms all caught | `m2_keys.log` |
| Keys | lost events | `dropped=0` on all four keys; every press has a release | `[keys]` summary lines |

Capture deviations (operator timing, kept as captured): the "UP once" window
got no press, a stray UP tap fell in the LEFT window, and the quick-tap step
has 2 RIGHT taps instead of 3. RIGHT's raw-edge count (6 = 3 presses × 2)
shows the board saw exactly the presses that were logged.

### M2 on-board results: LCD

Captured with `tools/capture_m2.py lcd`, firmware `15df5af` (clean build).
The init values and bus timing follow the board vendor's driver (see
`docs/test_sheets/m2_lcd.md`).

| Check | Measured | Source |
|---|---|---|
| Init | OK in 333 ms (two 100 ms reset waits + 120 ms sleep-out) | `m2_lcd.log` |
| Controller ID (RDDID) | `81 81 B3` | `m2_lcd.log` |
| Power mode (RDDPM) | `0x9C`: booster on, sleep out, normal mode, display on | `m2_lcd.log` |
| FSMC data lines | `lcd bus` PASS 6/6: D0..D7 written and read back at both levels. COLMOD reads back bits 2..0 only (`0x65` → `0x05`) | `m2_lcd.log` |
| Frame rate (TE pin PD6) | 16 917 µs period = **59.11 Hz** over 31 edges | `lcd te`, `m2_lcd.log` |
| Full-screen write | 240×240×2 B in **13.39 ms** (≈ 8.6 MB/s, about 116 ns per byte with 9+8 HCLK write timing) | `lcd fill`, `m2_lcd.log` |
| Colours, backlight, edges | white/red/green/blue correct, no dead lines; backlight off goes dark; white frame visible on all four edges | operator answers, `m2_lcd_cues.txt` |
| Orientation | **not recorded** (the corner questions were answered `y`); no photo | TODO (H): `capture_m2.py lcd --force` |

For the UI (M5): one full redraw takes about 13 ms, i.e. almost one frame
(16.9 ms), so a full refresh every 50 ms is affordable. Partial redraws (PRD 6.2)
are still the plan. The CLI task busy-waits during `lcd te`, for about 0.5 s.

### M2 on-board results: LED matrix

Captured with `tools/capture_m2.py matrix`; first run firmware `812b5f2`
(commit `448999d`), re-run after the fix with `9daf44f`.

| Check | Measured | Source |
|---|---|---|
| Frame on the wire | **545 µs** DMA start → complete (computed 459 bit periods × 1.19 µs = 546 µs) | `last_wire_us`, `m2_matrix.log` |
| Reliability | 26 frames, 0 timeouts, 0 DMA errors | `m2_matrix.log` |
| Colours | red, green, blue, white on all LEDs; GRB byte order correct | operator answers |
| Chain | 19 LEDs, lit one at a time in order 1 → 19 | `matrix chase 400`, operator count |
| Init | 812b5f2: **not** all off after init (buffer enabled while PA7 floated). 9daf44f: all off | both cue files |

TIM3 is shared with the IR transmitter (PB1 = TIM3_CH4): the IR step must
not reprogram TIM3 behind the matrix driver.

### M2 on-board results: RTC

Captured with `tools/capture_m2.py rtc`; first run firmware `c4464ec`
(commit `bb2ee1a`, rate check broken), re-run after the fix with `134d897`.

| Check | Measured | Source |
|---|---|---|
| Clock source | LSE (crystal X1); started in 369 ms on first init (agent check), LSI fallback not needed | `m2_rtc.log`, commit `c4464ec` |
| Rate vs HSE | 20 RTC s = 19 998 397 µs of CPU time, **+80.1 ppm**, identical in both runs (also +80.1 at 5 s and 13 s) | `rtc check 20` |
| Rate vs PC (derived) | HSE is −70 ppm vs the PC (M1: 999.93 ticks/s), so the RTC is about **+10 ppm** vs the PC, ≈ 0.9 s/day | M1 + this table |
| RESET | `kept_running=yes`, `time_valid=yes`; offset to the PC timestamps 1.136 s before and after the reset (no step) | `m2_rtc.log` |
| Input check | `rtc set 2026-02-30 …` → `RANGE_ERR`, time unchanged | `m2_rtc.log` |
| Power loss | no coin cell (holder empty): time is lost at power-off, `time_valid=no` after power-up | board photo, design |

Bug found by the first capture: the rate check's sleep used a signed 32-bit
cycle difference, which overflows after 12.8 s (fixed in `134d897`).

### M2 on-board results: storage

Captured with `tools/capture_m2.py storage`, firmware `f8699b4`, SD slot empty.

| Part | Check | Measured | Source |
|---|---|---|---|
| microSD | no card | `init NOT_PRESENT` in 3–4 ms (HAL error `0x4` = command response timeout), same on the second try; PF3 high | `m2_storage.log` |
| SPI flash | identity | JEDEC `EF 40 17`, SFDP v1.5, **8 MiB** (schematic: W25Q128 16 MB) | `flash` |
| SPI flash | write test, last 4 KB sector (blank) | erase 65–68 ms; 4096 B in 16 pages programmed in 9.6 ms (≈ 0.6 ms per page); 0 erase / verify / restore errors, twice | `flash test` |
| System | after the tests | heap min 40 928 B; CLI stack 854 of 1024 words free | `diag mem` |

With 8 MB, the flash holds settings and a fallback event log (PRD 6.2);
continuous sample capture stays on the UART / SD path.

### M2 on-board results: IR

Captured with `tools/capture_m2.py ir`, firmware `aab7b56`.

| Check | Measured | Source |
|---|---|---|
| Carrier | 38 000 Hz (168 MHz / 4421), 1/3 duty, TIM8_CH3N on PB1 | `ir` |
| Loopback | **20/20** and **64/64** bursts detected; 0 extra edges, 0 idle edges in the 50 ms before | `ir loop`, `ir loop 64` |
| Receiver timing | output low 232–233 µs after the burst starts, for 546–551 µs (bursts are 560 µs) | `ir loop` |
| Control | PB1 held low, same timing: **0/20** twice, so the loop is optical | `ir loop dry` |
| Remote | operator pressed a remote: 0 edges in 10 s. Inconclusive (remote type/aim/battery); the receiver itself is proven by the loopback | `ir rx 10`, cues |

EXTI8 (IR RX) and EXTI5 (key UP) share the EXTI9_5 vector; each handler
clears only its own pending bit.

### M2 on-board results: audio codec (control port)

Captured with `tools/capture_m2.py codec`, firmware `78bada8`.

| Check | Measured | Source |
|---|---|---|
| Presence | ACK at I2C2 `0x10` | `codec` |
| Register dump | 53 registers (0x00–0x34) in 6–7 ms; DACPOWER `0xC0` (outputs off), LOUT1VOL `0x00` | `codec` |
| Write path | LOUT1VOL `0x15`, `0x2A`, `0x00`, `0x3F` read back **4/4**, restored to `0x00` | `codec test` |
| Side effects | second dump identical to the first | `codec` |
| Bus | I2C2 119 transfers, 0 NACK / timeout / bus error | `i2c stats` |

The ES8388 has no chip-ID register; the ACK, the dump and the read-back
stand in for an ID check. The audio path (I2S3, microphone, headphone jack)
is not exercised: PackSense does not record or play audio (PRD §3).

### M2 on-board results: RW007 Wi-Fi

Captured with `tools/capture_m2.py wifi`, firmware `aa4b41a` (clean build,
product firmware: the boot self-test had already started the module and put it
back in reset). No network was joined.

| Check | Measured | Source |
|---|---|---|
| Boot | RST pulse on PG15 → INT (PG11) high after **666 ms** | `wifi` |
| Init | INIT + station mode + serial + firmware + MAC in 51 ms; firmware `RW007_2.1.0-a7a0` | `wifi` |
| Radio | scan 1: 3 networks in 1331 ms; scan 2: 5 networks in 1332 ms, both `SCAN_DONE`; RSSI −33 … −90 dBm, all WPA2-AES, one hidden | `wifi scan` ×2 |
| SPI link | 290 transfers, 0 INT timeouts, 0 stage-2 errors; 2 stage-1 header errors, both during init and both recovered by a retry (none in the 268 scan transfers) | `wifi stats` |
| Shared SPI2 bus | SPI flash test right after the scans: erase 69 ms, 4096 B programmed in 9.9 ms, 0 erase / verify / restore errors | `flash test` |
| System | heap min 40 928 B; lowest task stack headroom 101 words (IDLE) | `diag mem` |

Follow-up on the 2 stage-1 errors (agent check on the board, not an evidence
file): the counters were not reset between inits, so the 2 errors are one from
the boot self-test's init and one from the `wifi` init. In 4 of 4 resets the
first transfer after reset read 16 × `0xFF` (the module does not drive MISO in
its first chip-select window) and the retry succeeded. Fixed in `ed9640b`: one
wake window before INIT, and the counters restart at each init. After the fix:
boot self-test plus three `wifi` inits and a scan, 0 stage-1 errors and 0
retries; the flash test on the shared bus still showed 0 errors. The same
capture with the new firmware would show `wake window: no answer (expected)`
and `stage1_err=0`. The boot self-test row now shows the whole firmware version
(`e6a55a5`: `Wi-Fi OK 2.1.0-a7a0 off`, previously cut to `RW007_2.1.0-`).

Privacy: the log has network name lengths and vendor prefixes (OUI) only, no
names or full BSSIDs (`src/app/wifi_cmds.c`). The module's own serial number
and MAC are printed.

### M1 on-board results

From `evidence/m1_boot.log` (firmware git `8d71a0c`, RT-Spark HW1.1), checked with
`python tools/check_heartbeat.py evidence/m1_boot.log` → `RESULT: PASS`.

| Quantity | Measured | Source line / check |
|---|---|---|
| Clock source | HSE 8 MHz crystal → PLL M=8 N=336 P=2 Q=7, SYSCLK 168 MHz | `[boot] clock source`, `[boot] RCC:` |
| FreeRTOS tick vs PC clock | 999.93 ticks/s (−0.007 %), worst residual 14 ms over 190 s | least-squares fit over 191 `[hb]` lines |
| Heartbeat period | every 1000 ticks, n=1..191, no gaps | `[hb] tick=` |
| FreeRTOS heap (heap_4) | 40944 B usable; free 40944 B, minimum ever 40928 B | `[boot] FreeRTOS`, `[hb] heap_free= heap_min=` |
| Resets during capture | 2, both `PIN` (RESET key) | `[boot] reset cause:` |

Static memory, from `evidence/m1_build.txt` (linker `--print-memory-usage`):
FLASH 23104 B (2.20 %), RAM 49712 B (37.93 %, includes the 40 KB FreeRTOS heap),
CCM RAM 2192 B (3.34 %).

## M3–M5 (data capture + product firmware)

The full PRD §6.3 task set. Priorities, stacks and periods are from
`include/app_config.h`. All stacks and TCBs are static, in CCM RAM. HWM:
`TODO(measure)` until it comes from a `diag mem` dump in an `evidence/` log
(M5 parity / M6 soak); `tools/analyze_soak.py` prints the lowest value per task.

| Task | Function | Prio | Stack (words) | Period / activation | Communication | Watchdog check-in | HWM (measured) |
|---|---|---|---|---|---|---|---|
| `Sensor` | IMU 100 Hz, AP3216C every 5th tick; validates, time-stamps, flags each sample (PRD 6.4); DEGRADED after 3 failed reads, IMU re-init every 5 s; `fault inject` | 5 | 512 | `xTaskDelayUntil` 10 ms while sampling; sleeps on a notification otherwise | → `qSamples` (64 × `sample_t`); I2C2 mutex | every sample (≤ 500 ms idle) | TODO(measure) |
| `Infer` | 200-sample ring (stored as capture-log records, so quantised like the training data), validity check, 42 features, MLP, baseline, majority-of-3 smoothing; DWT timing | 4 | 1024 | every sample; a window every 50 samples (0.5 s hop) | ← `qSamples`; → `qResults` (4) to `Ui` | every loop (≤ 200 ms) | TODO(measure) |
| `Ui` | Boot self-test, mode state machine (`app_mode.c`), keys, LCD screens, LED / LED matrix / buzzer, alerts, climate rule, `[win]` log line | 3 | 768 | waits ≤ 50 ms on `qResults`; DIAG redraw 500 ms | ← `qResults`, key queue, mode requests; → `qLog` | every loop | TODO(measure) |
| `Capture` | CAPTURE mode (M3): trials to the flash log, its own keys / LCD / LED handling | 3 | 768 | 10 ms loop in the mode; request queue (1 s timeout) otherwise | ← `qSamples` (only in CAPTURE), request queue + ack semaphore | every loop (≤ 1 s) | TODO(measure) |
| `Env` | AHT 1 Hz; ALS/PS and AHT re-init while DEGRADED | 2 | 384 | `xTaskDelayUntil` 1000 ms | soft-I2C (own bus); latest T/RH copied into each sample | twice per period | TODO(measure) |
| `CLI` | Console commands (DIAG readouts, `mode`, `fault`, `selftest model`, `capture dump`…) | 2 | 1024 | blocks on console RX | console mutex; requests to `Ui` / `Capture` | **not monitored**: service commands may block for minutes (`capture dump`); a CLI that spins at prio 2 starves `Log` and `Heartbeat`, which then miss their check-ins | TODO(measure) |
| `Log` | Event ring in SPI flash 0x000000–0x00FFFF (2048 × 32 B, seq + checksum), `[event]` console line, `log` command | 1 | 1024 | blocks on `qLog` (1 s timeout) | ← `qLog` (16); SPI2 bus mutex | every loop (≤ 1 s) | TODO(measure) |
| `Heartbeat` | Watchdog feeder: every 500 ms checks the 2 s check-in window and feeds the IWDG only if all 7 monitored tasks checked in; optional `[hb]` line | 1 | 256 | `xTaskDelayUntil` 500 ms | check-in event group | is the feeder | TODO(measure) |

Watchdog: IWDG timeout 4 s (LSI / 64), frozen while a debugger halts the core.
A missing check-in is printed (`[wdg] check-in missing ...`), saved in RTC
backup register 19, reported after the reset and logged as a `WDG` event.
`fault hang <task>` stops that task's check-ins to demonstrate it.

Agent-side check on the board (not evidence; the numbers for the report come
from the M5/M6 logs): with the placeholder model, a 2 s window took 3.3–4.6 ms
for the features; window close → LCD/LED feedback 19–28 ms; `diag mem` showed
every task ≥ 101 words free; `fault hang infer` reset the board in ~8 s and the
next boot named `Infer`. With a temporary synthetic model: MLP 123 µs, and
`selftest model` matched 50/50 even while `Infer` ran at the same time.

## Interrupts

| IRQ | NVIC priority | Calls FreeRTOS API? | Purpose |
|---|---|---|---|
| TIM6_DAC | 4 | no (must not) | HAL tick (`HAL_GetTick`, HAL timeouts) |
| I2C2_EV / I2C2_ER | 5 | yes: `vTaskNotifyGiveIndexedFromISR` (slot 1) | I2C2 transfer completion / error → wakes the waiting task |
| USART1 | 6 | yes: `xStreamBufferSendFromISR` | console TX ring buffer; RX bytes → CLI stream buffer |
| EXTI0 / EXTI1 / EXTI4 / EXTI9_5 | 7 | yes: `xQueueSendFromISR` | arrow keys LEFT PC0, DOWN PC1, RIGHT PC4, UP PC5: timestamp each raw edge into the button queue; debounce (30 ms) runs in the consuming task. EXTI9_5 is shared with the IR receiver PF8 (`ir rx`, edge counting only) |
| EXTI15_10 | 7 | yes: `xSemaphoreGiveFromISR` | RW007 INT (PG11) falling edge → wakes the SPI transfer (`wifi` commands only) |
| SysTick | 15 | kernel | FreeRTOS tick |
| PendSV / SVC | 15 / — | kernel | context switch / first task start |

Rule: an ISR that calls a `...FromISR()` API must have priority number ≥ 5
(`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`). The kernel asserts this at
run time (`configASSERT` is on).
