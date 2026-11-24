# M2 test sheet: LED matrix (19 × SK6805)

**Who:** H (a person at the board). **Firmware:** `0.2.0-m2` from a clean tree
(the banner must not say `-dirty`).
**Result files:** `evidence/m2_matrix.log`, `evidence/m2_matrix_cues.txt`.

## Run it

Close any open serial monitor, then in PowerShell:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" "$env:USERPROFILE\Documents\BCA182 - LAB 4\tools\capture_m2.py" matrix
```

It asks you to press RESET, types the commands, and asks what you see. The
LEDs are driven at 16 of 255 on purpose (19 LEDs at full white draw a lot of
current), so look closely.

## What it checks

| Step | Command | The log shows | You check |
|---|---|---|---|
| Idle at reset | — | — | all off before the first command (buffer disabled by its pull-up) |
| Init | `matrix` | `init OK`, `frames=1 timeouts=0 dma_errors=0`, `last_wire_us` ≈ 545 | still all off |
| Colours | `matrix fill 16 0 0`, `0 16 0`, `0 0 16`, `16 16 16` | `sent: OK` each | every LED red, green, blue, white (green/red not swapped, none dark) |
| Off | `matrix off` | `all off: OK` | all dark |
| Chain | `matrix chase 400` | `chase led=0..18`, `19 of 19 frames sent OK` | one LED at a time; you count the positions (19 expected) and say where it starts and how it moves |

`last_wire_us` is DMA start to transfer complete: 19 LEDs × 24 bits + 3 idle
periods at 1.19 µs each = about 546 µs.

## Source of the bit timing

The SK6805 datasheet is not in `docs/hardware/vendor/` yet (see
[`SOURCES.md`](../hardware/SOURCES.md)). The bit timing (TIM3 at 8.4 MHz,
10 counts per bit, 2 counts high for a 0 and 7 for a 1), the green-red-blue
byte order and the TIM3_CH2 + DMA1 stream 5 channel 5 route come from the board
vendor's driver: `RT-Thread-Studio/sdk-bsp-stm32f407-spark`,
`libraries/Board_Drivers/led_matrix/drv_matrix_led.c`, commit `4c284fc`.

TIM3 is also the timer of the IR transmitter pin (PB1 = TIM3_CH4). The IR
driver must not reprogram TIM3 while the matrix is in use (M2 IR step).
