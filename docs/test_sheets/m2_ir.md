# M2 test sheet: IR transmitter + receiver

**Who:** H (one RESET press; a TV/aircon remote is optional).
**Firmware:** `0.2.0-m2` from a clean tree.
**Result files:** `evidence/m2_ir.log`, `evidence/m2_ir_cues.txt`.

## Run it

Close any open serial monitor, then in PowerShell:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" "$env:USERPROFILE\Documents\BCA182 - LAB 4\tools\capture_m2.py" ir
```

About 30 s. Keep the board flat; do not cover the bottom-left corner (the
IR receiver, below the silver AHT21).

## What it checks

| Step | Command | The log shows |
|---|---|---|
| Init | `ir` | carrier frequency (38 000 Hz expected), receiver idle level `high` |
| Loopback | `ir loop`, `ir loop 64` | bursts sent vs detected by the board's own receiver; receiver delay and low-pulse width (≈ the 560 µs burst) |
| Control | `ir loop dry` (twice) | same timing with PB1 held low, LED dark: **0 detected** expected. Pulses here would mean electrical crosstalk, not light |
| Remote (optional) | `ir rx 10` | frames and the first low pulse of each (an NEC remote starts with ≈ 9000 µs) |

## Design notes

- Carrier on **TIM8_CH3N** (PB1 AF3), not TIM3_CH4: TIM3 drives the LED
  matrix. Values (38 kHz, 1/3 duty) as in the board vendor's IR demo.
- Receiver edges are time-stamped in the EXTI8 ISR (shared EXTI9_5 vector
  with key UP); no FreeRTOS call in that path.
- Burst shape for the loop is NEC-like (560 µs carrier, 1690 µs gap), which a
  38 kHz demodulating receiver accepts.
