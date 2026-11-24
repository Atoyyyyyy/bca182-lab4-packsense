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

## Interrupts

| IRQ | NVIC priority | Calls FreeRTOS API? | Purpose |
|---|---|---|---|
| TIM6_DAC | 4 | no (must not) | HAL tick (`HAL_GetTick`, HAL timeouts) |
| I2C2_EV / I2C2_ER | 5 | yes: `vTaskNotifyGiveIndexedFromISR` (slot 1) | I2C2 transfer completion / error → wakes the waiting task |
| USART1 | 6 | yes: `xStreamBufferSendFromISR` | console TX ring buffer; RX bytes → CLI stream buffer |
| EXTI0 / EXTI1 / EXTI4 / EXTI9_5 | 7 | yes: `xQueueSendFromISR` | arrow keys LEFT PC0, DOWN PC1, RIGHT PC4, UP PC5: timestamp each raw edge into the button queue; debounce (30 ms) runs in the consuming task |
| SysTick | 15 | kernel | FreeRTOS tick |
| PendSV / SVC | 15 / — | kernel | context switch / first task start |

Rule: an ISR that calls a `...FromISR()` API must have priority number ≥ 5
(`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`). The kernel asserts this at
run time (`configASSERT` is on).
