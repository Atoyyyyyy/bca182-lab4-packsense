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
| 4 | 1.3" 240×240 LCD, ST7789V3 (CN2) | FSMC 8-bit, NE3 PG10, RS A18 PD13, RST PD3, TE PD6, BL PF9 | ☐ | ☐ | **Output / UI** | home, alerts, DIAG | |
| 5 | Red + blue indicator LED (D4) | PF12 (R), PF11 (B), active low | ☐ | ☐ | **Output**: state colour | RUN/GUARD, DIAG | |
| 6 | LED matrix, 19 × SK6805 (U6 buffer) | DATA PA7, EN PF2 (active low) | ☐ | ☐ | **Output**: alert animation | alarm, DIAG self-test | |
| 7 | Active buzzer (BUZ1) | PB0, active high | ☐ | ☐ | **Output**: audible alert | alerts, DIAG | |
| 8 | 4 arrow keys (SW2–SW5) + reset (SW6) | PC5 UP, PC1 DOWN, PC0 LEFT, PC4 RIGHT; active low | ☐ | ☐ | **User input** | ack, arm guard, mute, mode, capture labels | |
| 9 | RTC + 32.768 kHz crystal (X1) (+ backup battery? `____`) | internal RTC, LSE PC14/PC15 | ☐ | ☐ | **Logging**: timestamps | logs, DIAG | |
| 10 | microSD slot (SDIO) + card (size `____`) | PC8–PC12, PD2, CD PF3 | ☐ | ☐ | **Logging**: events + captures | LogTask, DIAG | |
| 11 | W25Q128 16 MB SPI flash (U10) | SPI2 PB13/PC2/PC3, CS PB12 | ☐ | ☐ | **Config / fallback log** | settings, DIAG | |
| 12 | RW007 Wi-Fi module (U12) | SPI2, CS PF10, INT PG11, RST PG15 | ☐ | ☐ | **Comms (optional)**: event report | LogTask, DIAG | |
| 13 | Onboard ST-LINK/V2.1 + VCP | SWD; USART1 PA9/PA10 | ☐ | ☐ | **Debug + data capture** | console, CAPTURE CSV | |
| 14 | IR transmitter (D6) + receiver IRM-H638T (U9) | TX PB1, RX PF8 | ☐ | ☐ | **Diagnostics** | DIAG IR loopback self-test | |
| 15 | ES8388 audio codec + mic + headphone jack (U11) | I2C2 `0x10`, I2S3 | ☑ | ☐ | **Diagnostics only** (no audio stored) | DIAG codec ID / level meter | answers `0x10` in `i2c scan` ([`m2_imu.log`](../../evidence/m2_imu.log)); codec ID: later M2 step |
| 16 | CAN1, RS485, PMOD1/2, USB OTG connectors | see pinmap | ☐ | n/a | Connectors only, not sensors (unless a module is attached) | — | |

Legend: ☐ = not yet checked · ☑ = confirmed with evidence · ✗ = absent/broken (→ `exceptions.md`)
