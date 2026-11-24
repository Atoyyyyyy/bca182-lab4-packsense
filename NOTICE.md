# Third-party notices and attribution

PackSense's own code is MIT-licensed (see `LICENSE`). The components below keep
their own licenses. Versions are filled in **when each component is actually
vendored or pinned**. An entry marked *TODO(pin)* has not been added to the
build yet.

| Component | Used for | License | Version / source | Status |
|---|---|---|---|---|
| FreeRTOS-Kernel | RTOS (`lib/FreeRTOS/`) | MIT | **V11.1.0**, vendored unmodified, archive SHA-256 in `lib/FreeRTOS/VENDORED.md` | in use (M1) |
| STM32CubeF4 HAL/LL + CMSIS device headers, startup and `system_stm32f4xx.c` | MCU drivers (via PlatformIO `framework = stm32cube`) | BSD-3-Clause (HAL), Apache-2.0 (CMSIS) | `framework-stm32cubef4` **1.28.3** (HAL **1.8.5**), from platform `ststm32@20.0.0` (pinned in `platformio.ini`) | in use (M1) |
| `include/stm32f4xx_hal_conf.h` | HAL configuration | BSD-3-Clause (ST) | copied from the Cube 1.28.3 template, edits marked "PackSense" | in use (M1) |
| `linker/STM32F407ZGTx_FLASH.ld` | Linker script | BSD-3-Clause (ST) | copied from PlatformIO `tool-ldscripts-ststm32` 0.3.0, edits marked "PackSense" | in use (M1) |
| `src/hal_timebase_tim6.c` | HAL tick on TIM6 | BSD-3-Clause (ST), adapted | based on Cube 1.28.3 `stm32f4xx_hal_timebase_tim_template.c` | in use (M1) |
| GNU Arm toolchain | Compiler, newlib-nano | GPL (tools), BSD-style (newlib) | `toolchain-gccarmnoneeabi` 1.70201.0 (GCC 7.2.1) | in use (M1) |
| CMSIS-DSP (only if used for the FFT) | `arm_rfft_fast_f32` | Apache-2.0 | TODO(pin) | optional |
| FatFs (ChaN) | SD-card filesystem (`lib/FatFs/`) | FatFs license (BSD-style, 1-clause) | TODO(pin), vendored in M2 | planned |
| scikit-learn, NumPy, PyYAML, matplotlib | Host-side ML pipeline (`ml/`) | BSD-3-Clause / BSD-3-Clause / MIT / PSF-based | scikit-learn 1.9.1, NumPy 2.5.5, PyYAML 6.0.4, matplotlib 3.11.2, pinned in `ml/requirements.txt` | in use (M4) |
| DejaVu Sans Mono (Regular, Bold) | LCD bitmap fonts: `tools/make_font.py` renders ASCII 32–126 into `src/ui/font_data.h` | Bitstream Vera Fonts license (DejaVu changes public domain) | the TTFs shipped with matplotlib 3.11.2 (`mpl-data/fonts/ttf`, license text in `LICENSE_DEJAVU` there) | in use (M5) |
| pyserial | Host capture script (`tools/capture.py`) | BSD-3-Clause | pinned in `ml/requirements.txt` (M3) | planned |

## Hardware documentation (referenced, not redistributed)

These documents are **not committed** because they have no redistribution
license. `tools/fetch_hw_docs.py` downloads them into `docs/hardware/vendor/`
and checks their SHA-256. See `docs/hardware/SOURCES.md`.

- RT-Spark (星火1号) schematic HW1.1, user manual V1.1.0, placement/BOM sheet:
  RT-Thread, from `github.com/RT-Thread-Studio/sdk-bsp-stm32f407-spark` (`docs/`).
- ICM-20608-G datasheet (TDK InvenSense), AP3216C datasheet (LITE-ON / Dyna Image),
  AHT10 datasheet v1.1 (Aosong): copies distributed inside the RT-Thread BSP packages.

No RT-Thread OS code is used. The mandatory stack is STM32Cube HAL/LL + FreeRTOS.

## Font notice (LCD text)

`src/ui/font_data.h` contains 1-bit bitmaps rendered from DejaVu Sans Mono
(regular and bold) by `tools/make_font.py`. The rendered glyphs are not
distributed as a font file or under the Bitstream, Vera or DejaVu names.

- Bitstream Vera Fonts: Copyright (c) 2003 by Bitstream, Inc. All Rights
  Reserved. Bitstream Vera is a trademark of Bitstream, Inc.
- DejaVu changes are in the public domain.
- Glyphs imported from Arev fonts are (c) Tavmjong Bah.

The full license text is the `LICENSE_DEJAVU` file next to the TTFs in
matplotlib's `mpl-data/fonts/ttf/` folder.
