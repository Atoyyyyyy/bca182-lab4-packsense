# M2 test sheet: indicator LED, buzzer, arrow keys

**Who:** H (a person at the board). **Firmware:** `0.2.0-m2` from a clean tree
(the banner must not say `-dirty`).
**Result files:** `evidence/m2_leds.log`, `evidence/m2_buzzer.log`,
`evidence/m2_keys.log`, each with a `_cues.txt` file.

## Run it

Close any open serial monitor, then in PowerShell:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" "$env:USERPROFILE\Documents\BCA182 - LAB 4\tools\capture_m2.py" io
```

`io` runs the three parts one after another (or use `leds`, `buzzer` or `keys`
alone; add `--force` to redo one). For each part it asks you to press RESET,
then types the commands for you.

LED colour and sound can only be checked by a person, so the script **asks you
a question** after each step. Answer `y`, `n` or a few words (for example
"blue is next to the USB port instead"). Your answers go into the
`_cues.txt` file with a timestamp. Answer honestly: a "no" is a finding, not a
failure.

## Where things are

Hold the board with the long pin header at the top and the USB ports on the right.

| Part | Where |
|---|---|
| Indicator LED D4 (red + blue) | just right of the big ST chip; it blinks red once per second after boot |
| Buzzer | round black part with a `+`, near the bottom middle |
| Arrow keys UP / DOWN / LEFT / RIGHT | bottom right. **RESET is the red key**: press it only when the script asks |

## What each part checks

| Part | Commands | You check / the log shows |
|---|---|---|
| LEDs | `led red on`, `led blue on`, both, off, `led auto` | colour and blink by eye (this also settles the M1 "1 Hz blink" check) |
| Buzzer | `buzzer beep 300`, `chirp`, `error`, `alarm` | silent at boot; 1 beep, 1 chirp, 3 beeps, ~3 s alarm that stops by itself |
| Keys | `keys 25` while you press keys on cue | one `press` + one `release` per key press, hold time of the long UP press (≥ 800 ms → `long=1`), `edges` = raw edges per change (1 = clean, more = contact bounce the debounce removed) |
