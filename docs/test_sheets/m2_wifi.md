# M2 test sheet: RW007 Wi-Fi module

**Who:** H (one RESET press, nothing else).
**Firmware:** `0.2.0-m2` from a clean tree.
**Result files:** `evidence/m2_wifi.log`, `evidence/m2_wifi_cues.txt`.

## Run it

Close any open serial monitor, then in PowerShell:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" "$env:USERPROFILE\Documents\BCA182 - LAB 4\tools\capture_m2.py" wifi
```

About 20 s. No network is joined and no password is needed.

## What it checks

| Step | Command | The log shows |
|---|---|---|
| Presence | `wifi` | RST pulse, INT high after boot (ms), INIT + station mode, serial number, firmware version, MAC |
| Radio | `wifi scan` (twice) | networks found, `SCAN_DONE`, RSSI range; per network RSSI, channel, security, BSSID vendor prefix (OUI), **name length only** |
| Shared bus | `flash test` | the SPI flash still passes with the RW007 on the same SPI2 bus |
| Link health | `wifi stats` | transfers, retries, stage errors, INT timeouts |
| System | `diag mem` | heap and stack headroom |

## Design notes

- SPI framing and command numbers follow the board vendor's rw007 package
  v2.0.1 (Apache-2.0), rewritten as a small synchronous FreeRTOS driver.
- SPI2 is shared with the flash; `spi2_bus.c` holds a mutex for every
  chip-select window. Both run at 10.5 MHz, mode 0.
- Privacy: network names and full BSSIDs are not printed, so the committed
  log does not publish nearby networks.
- Joining a network and an IP stack are out of M2 scope (Wi-Fi is the
  optional comms path, inventory row 12).
