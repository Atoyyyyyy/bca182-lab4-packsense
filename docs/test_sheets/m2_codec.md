# M2 test sheet: ES8388 audio codec (control port)

**Who:** H (one RESET press, nothing else).
**Firmware:** `0.2.0-m2` from a clean tree.
**Result files:** `evidence/m2_codec.log`, `evidence/m2_codec_cues.txt`.

## Run it

Close any open serial monitor, then in PowerShell:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" "$env:USERPROFILE\Documents\BCA182 - LAB 4\tools\capture_m2.py" codec
```

About 10 s. No sound is played and nothing is recorded.

## What it checks

| Step | Command | The log shows |
|---|---|---|
| Presence | `codec` | ACK at I2C2 `0x10` and all 53 control registers (0x00–0x34) |
| Write path | `codec test` | 4 values written to LOUT1VOL (0x2E) and read back, then the original restored |
| No side effects | `codec` again | the dump is identical to the first one |
| Bus health | `i2c stats` | 0 NACKs, timeouts, bus errors |

## Design notes

- The ES8388 has no chip-ID register, so the ACK, a plausible dump and the
  read-back test together stand in for an ID check.
- LOUT1VOL is a volume register; the headphone output stays powered down
  (DACPOWER is not written), so the test makes no sound.
- PackSense does not record or play audio (PRD §3). The I2S3 pins
  (PB3/PA15 shared with SWO/JTDI) are not configured.
- Register addresses as in the board vendor's `drv_es8388.h`.
