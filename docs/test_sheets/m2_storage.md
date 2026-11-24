# M2 test sheet: storage (microSD slot, SPI flash)

**Who:** H (one RESET press). **Firmware:** `0.2.0-m2` from a clean tree.
**Result files:** `evidence/m2_storage.log`, `evidence/m2_storage_cues.txt`.

## Run it

Close any open serial monitor, then in PowerShell:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" "$env:USERPROFILE\Documents\BCA182 - LAB 4\tools\capture_m2.py" storage
```

About 15 s after the RESET. The SD slot stays empty (no card in our kit).

## What it checks

| Step | Command | The log shows |
|---|---|---|
| SD, no card | `sd` (twice) | raw card-detect level on PF3; `init NOT_PRESENT` with the HAL error code; the second try behaves the same (no stuck state) |
| Flash identity | `flash` | JEDEC ID (manufacturer, type, capacity code); SFDP version and the density the chip itself reports |
| Flash write test | `flash test` (twice) | last 4 KB sector: saved, erased (all 0xFF?), 4096 B pattern programmed and verified, original content written back; erase and program times; error counts must be 0 |
| Still running | `diag mem` | heap and task stack high-water marks after the tests |

## The flash is not the part on the schematic

The schematic and our first pin map say **W25Q128 (16 MB)**. The chip on our
board answers JEDEC `EF 40 17` and its SFDP table says **8 MiB**, a
W25Q64-class part (the vendor BSP's code also calls it W25Q64). The pin map,
inventory and design notes now say 8 MB. H: read the marking on U10 and put it
in `inventory.md` row 11.

## Is the write test safe?

`flash test` uses the last 4 KB sector (0x7FF000 on an 8 MB chip). It reads
the sector into RAM first and writes it back at the end, so any content there
survives. The vendor's demo layout puts its "filesystem" partition at the end
of the chip; nothing in PackSense uses the flash yet.

## SD card: what is still to do

With a FAT32 card: `sd` should report the card type, size, CID and a valid
`55 AA` sector 0. That confirms the card-detect polarity too. The FAT file
write/read test (PACKSENS/SELFTEST.TXT) comes with `sdlog.c` (FatFs).
Until then the missing card is logged in [`exceptions.md`](../hardware/exceptions.md).
