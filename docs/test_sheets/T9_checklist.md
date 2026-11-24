# T9 checklist: privacy, safety, accessibility

PRD §3 (risks) and §9 T9. The agent filled in *How it is met* from the code and
docs; the team checks every item on the board or in the repo, ticks it and
writes what they saw if it differs. Unticked items stay open in the report.

Checked by (operator codes): ______ Date: ______ Firmware (`version`): ______

## Privacy

| # | Item | How it is met | Check (H) | OK |
|---|---|---|---|---|
| P1 | No audio or images are recorded | The ES8388 codec is used through its I2C control port only (register dump / read-back test); I2S is not compiled in (`HAL_I2S_MODULE_ENABLED` is off in `include/stm32f4xx_hal_conf.h`). There is no camera | `codec` prints registers only; nothing plays or records | ☐ |
| P2 | The event log holds states and times, not raw sensor data | `src/app/evlog.h`: each 32-byte record = sequence, uptime, RTC time, event type, mode, state, confidence, two event values, boot number, checksum, **and the bag temperature / RH at that moment** | `log 20`: lines show state, time and T/RH context only | ☐ |
| P3 | Training / test data carry no names | `data/README.md`: operator codes P1, P2, …; bag and location codes; `*_private*` files are git-ignored | `git grep` the team's names in `data/` and `evidence/`: no hits | ☐ |
| P4 | Nearby Wi-Fi networks are not published | `wifi scan` prints the name **length**, RSSI, channel, security and the vendor prefix (OUI) only, never the network name or full address (`src/app/wifi_cmds.c`) | `evidence/m2_wifi.log` has no network names | ☐ |
| P5 | Nothing leaves the device over a network | The product never joins a network: the RW007 is held in reset after the boot self-test (`ui.c`), there is no upload code, and `include/secrets.h` is not used by the firmware | `wifi stats` after a soak: `rx` does not grow while the module is off | ☐ |
| P6 | The raw capture log on the board is erased after download | `tools/capture.py wipe` erases the SPI flash capture area (M3 procedure) | `tools/capture.py status` shows `log: 0 of ... records` after the last download | ☐ |

## Safety

| # | Item | How it is met | Check (H) | OK |
|---|---|---|---|---|
| S1 | Rough-handling trials never risk a laptop | Every ROUGH procedure (M3, T1, T4, T5) says **dummy load only**, no real drops, tosses ≤ 50 cm onto a cushion or bed | Confirm all ROUGH trials used the dummy load | ☐ |
| S2 | Buzzer loudness is moderate | Active buzzer (fixed pitch and loudness, `src/drivers/buzzer.h`); alarm pattern on/off, silenced by any key or the mute hold | Listen at arm's length: not painful. Optional: phone sound-meter app at 30 cm, reading: ______ dB (TODO(measure)) | ☐ |
| S3 | No heating tests with heat guns or open flame | PRD §12: warm the AHT gently by hand/breath only; climate thresholds can be lowered with `climate set` for a demo instead of heating | Confirm the demo used breath / `climate set` | ☐ |
| S4 | Tethered tests stay on level ground | `docs/test_sheets/m6_tests.md`: no stairs, no running while the USB cable is attached | Confirm | ☐ |
| S5 | The device is described honestly | README and report: experimental student prototype, not a certified security, anti-theft or impact-protection device | Read the README footer | ☐ |
| S6 | A crashed task cannot leave the alarm or outputs stuck | IWDG (4 s) is fed only when all monitored tasks check in (`src/app/wdg.c`); a reset turns the buzzer and LEDs off (pins low at init) | T6 case (e): after the watchdog reset the buzzer is silent and the board is back in RUN | ☐ |

## Accessibility

| # | Item | How it is met | Check (H) | OK |
|---|---|---|---|---|
| A1 | Every alert is both audible and visible | `src/app/ui.c` `on_result`: ROUGH → chirp + orange banner "Rough handling detected" + orange LED matrix; ACCESS while guarded → alarm sound + red flashing screen "ACCESS! bag opened while guarded" + red LED + red matrix; sensor fault → three beeps + red LED + "Sensor fault - <name>"; climate → three beeps + banner + "HOT/HUMID! check laptop" | See each in T4 / T6 / `m5_func` (sound and screen) | ☐ |
| A2 | The buzzer can be muted, alerts stay visible | DOWN held ~1 s toggles mute; header shows `MUTE`; banners, LEDs and the alarm screen still work | T7 mute steps; with mute on, raise `alarm test`: red screen, no sound | ☐ |
| A3 | State readable at a glance | State word in the large bold font, 24 × 48 px per character on the 240 × 240 LCD, colour per state, `?` when unsure, confidence underneath | Read the state from about 50 cm | ☐ |
| A4 | Colour is never the only cue | The state is always written as a word (STILL / CARRIED / ROUGH / ACCESS); alerts are text banners; GUARD is written in the header (not just red) | Confirm with the LCD | ☐ |
| A5 | Keys are hard to trigger by accident, easy when it matters | Arm/disarm and mute need a ~1 s hold (`BUTTON_LONG_PRESS_MS` 800 ms); **any** key acknowledges the alarm; the footer shows the key hints | T7 | ☐ |

Signatures (all three members): ______________________________
