# M2 test sheet: LCD (ST7789V3, 240×240, FSMC 8-bit)

**Who:** H (a person at the board). **Firmware:** `0.2.0-m2` from a clean tree
(the banner must not say `-dirty`).
**Result files:** `evidence/m2_lcd.log`, `evidence/m2_lcd_cues.txt`, and a photo
`evidence/m2_lcd_test.jpg` of the test pattern.

## Run it

Close any open serial monitor, then in PowerShell:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" "$env:USERPROFILE\Documents\BCA182 - LAB 4\tools\capture_m2.py" lcd
```

It asks you to press RESET, then types the commands. What the screen shows can
only be checked by a person, so after each step it **asks what you see**. Your
answers go into the `_cues.txt` file. Hold the board with the long pin header
at the top and the USB ports on the right the whole time: "top-left" means the
screen as you see it then.

## What it checks

| Step | Command | The log shows | You check |
|---|---|---|---|
| Init | `lcd` | `init OK`, ID bytes, `power_mode` decode: `sleep_out=1 normal=1 display_on=1` | backlight on, screen evenly black |
| Bus | `lcd bus` | MADCTL/COLMOD written and read back; `PASS` = every data line D0..D7 seen high and low, in both directions | — |
| Tearing effect | `lcd te` | 31 rising edges on PD6 and the frame period / rate | — |
| Colours | `lcd fill white/red/green/blue` | time for a full 115 200-byte write | each fill is the named colour (red/blue not swapped), no dead lines |
| Backlight | `lcd bl off`, `lcd bl on` | — | screen goes dark, then lit again |
| Orientation | `lcd test` | — | white frame on all four edges (no rows/columns cut off); corner colours; bar order |

**Expected test pattern** (if the panel's scan direction matches the board
orientation): corners top-left red, top-right green, bottom-left blue,
bottom-right yellow; middle bars red, green, blue from left to right. A
different arrangement is a finding, not a failure: it tells us which
MADCTL (0x36) rotation/mirror bits the UI needs.

## Source of the init values

The ST7789V3 datasheet is not in `docs/hardware/vendor/` yet (see
[`SOURCES.md`](../hardware/SOURCES.md)). The init register values and the
FSMC timing come from the board vendor's driver for this panel:
`RT-Thread-Studio/sdk-bsp-stm32f407-spark`, `libraries/Board_Drivers/lcd/drv_lcd.c`,
commit `4c284fc`. The register read-back behaviour (COLMOD returns only bits
2..0, so `0x65` reads back as `0x05`) was seen on our board in an agent check
before the capture.
