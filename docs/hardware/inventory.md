# Hardware inventory and component-to-function matrix

**How to fill this in (H):** for each row, look at **your physical kit**. Tick
*Fitted?* if the part is on the board or plugged in. Tick *Works?* only when an
`evidence/m2_<module>.log` (or photo) shows it working. Then put that evidence
file in the *Evidence* column. Anything missing or broken goes to
[`exceptions.md`](exceptions.md).

- Board revision printed on PCB: `HW: V1.1`, date code `24/23` (schematic used: HW1.1, matches). Evidence: [`evidence/m0_board_back.jpg`](../../evidence/m0_board_back.jpg)
- Kit checked by: `__________` on `____-__-__`
- Pin/bus details: see [`pinmap.md`](pinmap.md). Every assignment there is
  cited to a schematic sheet.

| # | Component (schematic ref) | Bus / pins (from schematic) | Fitted? | Works? | Role in PackSense | Used in | Evidence |
|---|---|---|---|---|---|---|---|
| 1 | ICM-20608-G 6-axis IMU (U5) | I2C2 PF1/PF0, `0x68`, INT PG12 | ☑ | ☑ | **Primary sensing**: motion | ML features, DIAG | [`m2_imu.log`](../../evidence/m2_imu.log): WHO_AM_I 0xAF, 100.01 Hz, 0 errors, tilt seen |
| 2 | AP3216C ALS + proximity + IR (U7) | I2C2, `0x1E`, INT PG13 | ☑ | ☑ | **Primary sensing**: light / opening / hand near | ML features, DIAG | [`m2_als.log`](../../evidence/m2_als.log): hand over sensor → lux 0, PS 1023, near |
| 3 | AHT21 (or AHT10, **read marking**: `______`) temp/RH (U8) | soft-I2C PE0/PE1, `0x38` | ☑ | ☑ | **Context sensing**: bag climate | climate rule, DIAG | [`m2_env.log`](../../evidence/m2_env.log): AHT2x by CRC; breath → RH 67 → 74 % |
| 4 | 1.3" 240×240 LCD, ST7789V3 (CN2) | FSMC 8-bit, NE3 PG10, RS A18 PD13, RST PD3, TE PD6, BL PF9 | ☑ | ☑ | **Output / UI** | home, alerts, DIAG | [`m2_lcd.log`](../../evidence/m2_lcd.log): ID `81 81 B3`, bus PASS, TE 59.11 Hz, colours/backlight/edges OK (operator). Orientation + photo: TODO (H) |
| 5 | Red + blue indicator LED (D4) | PF12 (R), PF11 (B), active low | ☑ | ☑ | **Output**: state colour | RUN/GUARD, DIAG | [`m2_leds.log`](../../evidence/m2_leds.log): red, blue and both at D4; 1 Hz heartbeat (operator: all yes) |
| 6 | LED matrix, 19 × SK6805 (U6 buffer) | DATA PA7, EN PF2 (active low) | ☑ | ☑ | **Output**: alert animation | alarm, DIAG self-test | [`m2_matrix.log`](../../evidence/m2_matrix.log): 19 LEDs lit in chain order, red/green/blue/white on all, 0 DMA errors (operator: all yes) |
| 7 | Active buzzer (BUZ1) | PB0, active high | ☑ | ☑ | **Output**: audible alert | alerts, DIAG | [`m2_buzzer.log`](../../evidence/m2_buzzer.log): silent at reset; beep, chirp, 3-beep error, 3 s alarm that stops (operator: all yes) |
| 8 | 4 arrow keys (SW2–SW5) + reset (SW6) | PC5 UP, PC1 DOWN, PC0 LEFT, PC4 RIGHT; active low | ☑ | ☑ | **User input** | ack, arm guard, mute, mode, capture labels | [`m2_keys.log`](../../evidence/m2_keys.log): each printed label reports its own pin; long press 2009 ms; debounce removes bounce; 0 dropped |
| 9 | RTC + 32.768 kHz crystal (X1) (+ backup battery? **none: holder empty**) | internal RTC, LSE PC14/PC15 | ☑ | ☑ | **Logging**: timestamps | logs, DIAG | [`m2_rtc.log`](../../evidence/m2_rtc.log): runs on LSE, keeps time through RESET, +80.1 ppm vs HSE (≈ +10 ppm vs PC). No coin cell: time lost at power-off |
| 10 | microSD slot (SDIO) + card (**no card in the kit**) | PC8–PC12, PD2, CD PF3 | ☑ slot / ✗ card | ☐ | **Logging**: events + captures | LogTask, DIAG | [`m2_storage.log`](../../evidence/m2_storage.log): no card → `NOT_PRESENT`, no crash. See `exceptions.md` #1 |
| 11 | SPI flash (U10): schematic W25Q128 16 MB, **board reads 8 MB (JEDEC EF 40 17, W25Q64-class)**; marking `______` | SPI2 PB13/PC2/PC3, CS PB12 | ☑ | ☑ | **Config / fallback log** | settings, DIAG | [`m2_storage.log`](../../evidence/m2_storage.log): JEDEC EF 40 17, SFDP 8 MiB, erase/program/verify/restore 0 errors |
| 12 | RW007 Wi-Fi module (U12) | SPI2, CS PF10, INT PG11, RST PG15 | ☐ | ☐ | **Comms (optional)**: event report | LogTask, DIAG | |
| 13 | Onboard ST-LINK/V2.1 + VCP | SWD; USART1 PA9/PA10 | ☑ | ☑ | **Debug + data capture** | console, CAPTURE CSV | [`m1_boot.log`](../../evidence/m1_boot.log): every evidence log came over the VCP (115200 8N1, COM3); firmware flashed over SWD for each capture |
| 14 | IR transmitter (D6) + receiver IRM-H638T (U9) | TX PB1, RX PF8 | ☑ | ☑ | **Diagnostics** | DIAG IR loopback self-test | [`m2_ir.log`](../../evidence/m2_ir.log): loopback 20/20 + 64/64, dry control 0/20 (optical). Remote test inconclusive (0 edges) |
| 15 | ES8388 audio codec + mic + headphone jack (U11) | I2C2 `0x10`, I2S3 | ☑ | ☑ control port | **Diagnostics only** (no audio stored) | DIAG codec register check | [`m2_codec.log`](../../evidence/m2_codec.log): 53 registers read, write/read-back 4/4 and restored, dump unchanged. No chip-ID register; I2S / mic / jack not tested (no audio by design) |
| 16 | CAN1, RS485, PMOD1/2, USB OTG connectors | see pinmap | ☐ | n/a | Connectors only, not sensors (unless a module is attached) | — | |

Legend: ☐ = not yet checked · ☑ = confirmed with evidence · ✗ = absent/broken (→ `exceptions.md`)
