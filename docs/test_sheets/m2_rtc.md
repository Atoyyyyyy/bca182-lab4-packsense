# M2 test sheet: RTC (internal RTC on the 32.768 kHz LSE crystal)

**Who:** H (one RESET press). **Firmware:** `0.2.0-m2` from a clean tree.
**Result files:** `evidence/m2_rtc.log`, `evidence/m2_rtc_cues.txt`.

## Run it

Close any open serial monitor, then in PowerShell:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" "$env:USERPROFILE\Documents\BCA182 - LAB 4\tools\capture_m2.py" rtc
```

About 1.5 minutes. It asks for RESET at the start and once more in the middle.

## What it checks

| Step | Command | The log shows |
|---|---|---|
| Clock source | `rtc` | `source=LSE` (an `LSI` warning means the crystal X1 did not start), `kept_running`, `lse_start_ms` |
| Set from the PC | `rtc set {now}` | the PC's local time when the command was sent (the script fills it in); `time_valid=yes` |
| Rate | `rtc check 20`, twice | 20 RTC seconds timed with the CPU cycle counter: RTC deviation in ppm **against the HSE crystal** |
| RESET | press RESET, then `rtc` | `kept_running=yes`, `time_valid=yes`, and the time still matches the PC timestamp at the start of the log line |
| Input check | `rtc set 2026-02-30 10:00:00` | `RANGE_ERR` (no 30 February), time unchanged |

## Reading the rate

`rtc check` compares two crystals: the RTC's LSE and the 8 MHz HSE that clocks
the CPU. Neither is a reference. M1 measured the HSE against the PC clock:
FreeRTOS ran 999.93 ticks/s, i.e. the HSE is about **−70 ppm** against the PC
(`evidence/m1_boot.log`, `docs/task_table.md`). So

    RTC vs PC  ≈  (RTC vs HSE)  +  (HSE vs PC)  ≈  rtc check result − 70 ppm.

The log line timestamps are PC time, so the RTC time printed by `rtc` can also
be compared with them directly over the length of the capture.

The detection of each second edge has about ±61 µs jitter (shadow-register
update every 2 LSE cycles), i.e. about ±3 ppm over 20 s.

## Known limits

- No coin cell in the holder (board photo, M0): the time is lost when the USB
  power is removed. After power-up `rtc` reports `time_valid=no`.
- RTC registers are written only when you run `rtc set`; nothing sets the time
  automatically yet (the PC sync is a console command, PRD 8.6).
