# Hardware document sources

Run `python tools/fetch_hw_docs.py` to download these into
`docs/hardware/vendor/` (git-ignored, no redistribution license). The script
checks the SHA-256 values below. A mismatch means upstream changed, so the pin
map must be re-checked.

Upstream: <https://github.com/RT-Thread-Studio/sdk-bsp-stm32f407-spark> (branch `main`, fetched 2026-11-22)

| Local file | Upstream path | SHA-256 |
|---|---|---|
| `RT-Spark_schematic_V1.0.pdf` (title block: Rev HW1.1, 2023-06-10) | `docs/星火1号开发板原理图_V1_0.pdf` | `1442c30c314f6a230f87c8a4ee92c383731de2694737e7e733b11add17457d3f` |
| `RT-Spark_user_manual_V1.1.0.pdf` | `docs/星火1号用户手册_V1.1.0.pdf` | `91cb0b2bb84214df50a77c3f1b9213684c3153db895714d1ff7fdfda3c495750` |
| `RT-Spark_placement_BOM.html` (interactive BOM) | `docs/星火1号开发板_点位&BOM.html` | `26f1058c7361031648feb592cfee2695ad12896ab0a3311bdf93a490606f5a44` |
| `ICM-20608_datasheet.pdf` | `projects/03_driver_axis/packages/icm20608-v1.0.0/icm20608_datasheet.pdf` | `99d1aa0c8bd00f51b104d4f9fb8328134b24c614dd0d063758989fef42b16973` |
| `AP3216C_datasheet.pdf` | `projects/03_driver_als_ps/packages/ap3216c-v2.0.0/ap3216c_datasheet.pdf` | `7e05f2388d8fb13da08391811655c9ccda24d26caa2c5fd383008bdd592ad86f` |
| `AHT10_datasheet_v1.1.pdf` | `projects/03_driver_temp_humi/packages/aht10-v2.1.0/aht10_datasheet_v1_1_181220.pdf` | `a6055ef378d97eb28c4cd5de23878c7b6ba4633331c65d3dcf39bae66ac4086e` |

## Still needed (H)

- **AHT21 datasheet** (Aosong). The schematic says AHT21, but the BSP only ships the AHT10 sheet.
- **ST7789V3 datasheet** (Sitronix) for the LCD init sequence.
- **W25Q64JV datasheet** (Winbond; the fitted chip reports JEDEC `EF 40 17` / 8 MiB, not the schematic's W25Q128), **ES8388 datasheet** (Everest), **RW007 docs** (RT-Thread), **SK6805 datasheet** (for LED-matrix timing).
- **STM32F407 reference manual RM0090 + datasheet DS8626** (ST).

Download these from the manufacturers' sites into `docs/hardware/vendor/` and
add their links here. M2 drivers for these parts wait until the sheet is present.
