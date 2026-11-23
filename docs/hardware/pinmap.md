# RT-Spark pin map (extracted from the schematic)

**Source:** RT-Spark (星火1号) schematic, title block *Rev HW1.1, 2023-06-10* (file
`RT-Spark_schematic_V1.0.pdf`, SHA-256 `1442c30c…457d3f`, fetched by
`tools/fetch_hw_docs.py`). Sheet numbers below are the schematic's own `Id: n/10`.

**Extraction:** read by the agent on 2026-11-22 from the rendered
schematic sheets. **Status of every row: `S` = taken from the schematic, not yet
confirmed on the physical board.** A row becomes `✔` after the human confirms it
on the board (silkscreen, chip marking, or a working M1/M2 test log in `evidence/`).
Firmware may use `S` rows for bring-up. A row that disagrees with the board is
fixed here first, then in code.

> **Board revision check: done (2026-11-23).** The back of our PCB reads
> **`HW: V1.1`**, date code `24/23`, `www.rt-thread.org`
> (photo: [`evidence/m0_board_back.jpg`](../../evidence/m0_board_back.jpg),
> SHA-256 `8aa6d782…97d8411`). This matches the schematic's HW1.1, so this pin map
> applies to our board.

## Clocks

| Item | Value | Sheet | Status |
|---|---|---|---|
| HSE crystal X2 | **8 MHz** (12 pF), PH0/PH1 | 5/10 | S |
| LSE crystal X1 | **32.768 kHz** (12 pF), PC14/PC15 | 5/10 | S |
| PC13 | not connected | 5/10 | S |
| BOOT0 | 10 kΩ pull-down (R4) | 5/10 | S |

Target: SYSCLK 168 MHz from HSE 8 MHz via PLL (M=8, N=336, P=2, Q=7: 48 MHz for USB/SDIO). This is computed from the 8 MHz HSE, so it must be checked in the M1 boot log.

## Debug / console

| Function | MCU pin | Notes | Sheet | Status |
|---|---|---|---|---|
| USART1_TX → ST-LINK VCP_RXD | PA9 | onboard ST-LINK/V2.1 (STM32F103CBT6) virtual COM port | 3, 5, 12 | S |
| USART1_RX ← ST-LINK VCP_TXD | PA10 | | 3, 5, 12 | S |
| SWDIO / SWCLK | PA13 / PA14 | to onboard ST-LINK | 5, 12 | S |
| SWO (JTDO) | PB3 | **shared with I2S3_SCK** (audio codec) | 5 | S |
| JTDI | PA15 | **shared with I2S3_WS** | 5 | S |
| NRST | pin 25 | reset key SW6 + ST-LINK T_RESET | 6, 12 | S |

## I2C buses and devices

| Bus | SCL | SDA | Pull-ups | Devices (7-bit addr) | Sheet | Status |
|---|---|---|---|---|---|---|
| **I2C2 (hardware)** | PF1 | PF0 | 5.1 kΩ (R1, R2) | **ICM-20608-G** `0x68` (AD0 → GND), **AP3216C** `0x1E`, **ES8388** audio codec `0x10` | 5, 6, 9 | S |
| **SOFT_I2C (bit-banged GPIO)** | PE0 | PE1 | 5.1 kΩ (R6, R7) | **AHT21** temp/RH `0x38` | 5, 6 | S |

Interrupt lines: `ICM_INT` → **PG12**, `APS_INT` (AP3216C) → **PG13**.

> ⚠ **AHT10 vs AHT21:** the schematic labels U8 as **AHT21**, while the PRD and the
> RT-Thread BSP package say AHT10. Both use 0x38 and the same 0xAC trigger
> command. AHT2x replaces the AHT10 `0xE1` init with a status-word check (0x18).
> **H: read the chip marking** and record it in `inventory.md`. The driver will
> support both.

## LCD (sheet 8/10)

| Item | Value | Status |
|---|---|---|
| Panel | 1.3" 240×240, controller **ST7789V3**, 24-pin FPC | S |
| Interface | **FSMC, 8-bit 8080 parallel** (`IM1/2` tied for 8-bit, check during M2) | S |
| Chip select | FSMC_NE3 → **PG10** (Bank 1, region 3 → base `0x6800_0000`) | S |
| D/C (RS) | FSMC_A18 → **PD13** (8-bit bus: data address = base + `1<<18`) | S |
| Data D0..D7 | PD14, PD15, PD0, PD1, PE7, PE8, PE9, PE10 | S |
| RD / WR | FSMC_NOE PD4 / FSMC_NWE PD5 | S |
| Reset | GPIO_LCD_RST → **PD3** | S |
| Tearing effect | GPIO_LCD_TE ← **PD6** | S |
| Backlight | GPIO_LCD_BL → **PF9**, drives an S8050 NPN low-side, **active high** | S |

## Indicators and outputs

| Item | MCU pin | Polarity / drive | Sheet | Status |
|---|---|---|---|---|
| LED red | **PF12** | LED anode to +3V3 via 2 kΩ (R17) → **active low** | 5, 6 | S |
| LED blue | **PF11** | active low via 2 kΩ (R16) | 5, 6 | S |
| LED green | **no MCU pin found** | `LED_GREEN` appears on the hierarchy sheets, but sheet 5 routes only `GPIO_LED_R`/`GPIO_LED_B` and D4 is drawn with two inputs | 3, 5, 6 | ⚠ verify |
| Buzzer | **PB0** | **active (self-oscillating) buzzer** via S8050 NPN, active high. Plain GPIO on/off, no PWM tone needed | 5, 6 | S |
| LED matrix | DATA **PA7**, EN **PF2** | **19 × SK6805** (WS2812-style serial RGB) behind a 74LVC1G125 buffer. `EN` is the buffer's /OE with a 10 kΩ pull-up → drive **low** to enable. PA7 is **shared with PMOD1 MOSI** | 5, 6 | S |
| LED_EXT_IO | PC6 | expansion connector | 5, 11 | S |
| IR transmitter | **PB1** | IR LED via S8050 NPN, active high | 5, 6 | S |
| IR receiver | **PF8** | IRM-H638T demodulating receiver (38 kHz class), output active low | 5, 6 | S |

## User input

| Key | MCU pin | EXTI line | Wiring | Status |
|---|---|---|---|---|
| UP (SW2) | **PC5** | EXTI5 (EXTI9_5_IRQn) | to GND when pressed, no external pull-up drawn → **internal pull-up, active low** | S |
| DOWN (SW4) | **PC1** | EXTI1 | same | S |
| LEFT (SW3) | **PC0** | EXTI0 | same | S |
| RIGHT (SW5) | **PC4** | EXTI4 | same | S |
| RESET (SW6) | NRST | — | hardware reset | S |

The four EXTI lines are all distinct, so there is no EXTI conflict.

## Storage

| Item | Pins | Notes | Sheet | Status |
|---|---|---|---|---|
| microSD (SDIO 4-bit) | D0..D3 = PC8..PC11, CK = PC12, CMD = PD2 | card detect `GPIO_CARD_DETECT` → **PF3** | 5, 7 | S |
| SPI flash **W25Q128** (16 MB) | SPI2: SCK **PB13**, MISO **PC2**, MOSI **PC3**, CS0 = SPI2_NSS **PB12** | shared SPI2 bus with RW007 → needs a bus mutex | 5, 7 | S |

## Wireless

| Item | Pins | Notes | Sheet | Status |
|---|---|---|---|---|
| **RW007** Wi-Fi (SPI) | SPI2 (shared), CS = `GPIO_SPI2_CS1` **PF10**, INT **PG11**, RST **PG15** | module may be socketed. **H: check if fitted** | 5, 10 | S |

## Audio (not in the original PRD §4)

| Item | Pins | Notes | Sheet | Status |
|---|---|---|---|---|
| **ES8388** codec | I2C2 `0x10`; I2S3: MCK PC7, SCK PB3, SD PB5, ext_SD PB4, WS PA15 | onboard microphone (MIC) and a 3.5 mm headphone jack (PJ-320A). Shares PB3/PA15 with SWO/JTDI | 9 | S |

PackSense does **not** record audio (privacy, PRD §3). The codec is reached only from
DIAG (ID/register read, and optionally a short test tone or a mic level meter that
stores no samples).

## Expansion (connectors only, not sensors)

| Connector | Pins | Notes | Status |
|---|---|---|---|
| CAN1 | PB8 RX, PB9 TX | transceiver on the expansion sheet | S |
| RS485 | USART6 PG14 TX / PG9 RX, DE/RE **PG8** | | S |
| PMOD1 | PA4–PA7, PE2–PE5 | PA7 shared with LED-matrix data | ✔ silkscreen |
| PMOD2 | PD11, PD12, PB10, PB11, PF4–PF7 | | ✔ silkscreen |
| USB OTG FS | PA11 / PA12 | | S |

### PMOD silkscreen (from `evidence/m0_board_back.jpg`, back of board)

Read top to bottom as printed on the back. The square pad is the first signal row
(PD11 on PMOD2, PA4 on PMOD1).

| PMOD2 left | PMOD2 right | | PMOD1 left | PMOD1 right |
|---|---|---|---|---|
| 3.3V | 3.3V | | 3.3V | 3.3V |
| GND | GND | | GND | GND |
| PD12 | PF4 | | PA5 | PE4 |
| PB11 | PF5 | | PA6 | PE3 |
| PB10 | PF6 | | PA7 | PE5 |
| PD11 ◼ | PF7 | | PA4 ◼ | PE2 |

### Other headers printed on the back

| Header | Silkscreen | Notes | Status |
|---|---|---|---|
| 3-pin, right edge above PMOD1 | `5V  DAT  GND` | Data pin is probably `LED_EXT_IO` → **PC6** (sheet 11), but the silkscreen does not name the MCU pin | ⚠ verify on sheet 11 |
| Power, left side | 3 × `5V`, 3 × `GND`, 3 × `3.3V`, 3 × `GND` | power only | ✔ silkscreen |
| Test pad | `BOOT0` | pad only (R4 pull-down per sheet 5) | ✔ silkscreen |
| Pads near the 2×20 header | `J1`–`J4` | purpose not on the silkscreen | ⚠ unknown |

## Free GPIOs (avoid for new wiring unless confirmed)

`GPIO_PA0–PA3, PA8, PB2, PB6, PB7, PB14, PB15, PD7–PD10, PE6, PE11–PE15, PF13–PF15, PG0–PG7`

### 2×20 expansion header (silkscreen, `evidence/m0_board_back.jpg`)

Printed on the **back** of the board, left to right as seen from the back. The
outer row is the one nearer the board edge. Seen from the component side, the
order is mirrored. `⏚` = ground symbol.

| Col | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 | 17 | 18 | 19 | 20 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **Outer** | PE11 | PE13 | PE15 | ⏚ˣ | PD9 | ⏚ˣ | PG7 | PG6 | PG4 | PG2 | ⏚ | PB2 | PG0 | ⏚ | PA0 | PA3 | PA2 | ⏚ | 5V | 5V |
| **Inner** | ⏚ | PE12 | PE14 | PD8 | PD10 | PF15 | PD7 | ⏚ | PG5 | PG3 | PG1 | 3.3V | PB15 | PB14 | PA1 | ⏚ | PA8 | PB6 | PB7 | 3.3V |

- ⏚ˣ: the two ground symbols at outer columns 4 and 6 have a small `x` next to them.
  The meaning is not clear from the photo. **H: continuity-check them to GND**
  before using them as ground.
- Every GPIO on this header is in the schematic's free-GPIO list above (✔ by silkscreen).
- **PE6, PF13 and PF14** are in the free list but are **not** on this header (not
  broken out, or not found on the silkscreen).

## Internal resources the firmware plans to use (MCU-side choices, not board facts)

| Resource | Use | Reason |
|---|---|---|
| SysTick | FreeRTOS tick only (1 kHz, priority 15) | PRD §6.1 |
| TIM6 | HAL timebase, 1 kHz, NVIC priority **4** (never calls FreeRTOS) | PRD §6.1 |
| USART1 | console, 115200 8N1, NVIC priority **6** | ST-LINK VCP |
| DWT CYCCNT | timing and run-time stats (extended to 64 bits in software) | PRD §8 |
| IWDG | watchdog | PRD §6.3 |
| RTC on LSE | timestamps | X1 fitted (if the LSE fails to start, fall back to LSI and log it) |
| CCM RAM (64 KB @ `0x1000_0000`) | task stacks / feature buffers only (**no DMA**). `.ccmram` is NOLOAD and zeroed in `main()`; use `CCM_BSS` | F407 CCM is CPU-only |
