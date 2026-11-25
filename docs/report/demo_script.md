# Demo video: script and shot list (PRD §12)

Target length 6–8 minutes, one take per shot, phone on a stand pointed at the
board (LCD readable) with the laptop console visible where a shot says so.
Each member presents at least one part (PRD M7: everyone must be able to
explain the firmware, the model, the hardware roles and the results).
Put the final link in `evidence/video_links.md`.

Before filming: real model flashed (banner without `-dirty`), event log
cleared (`log clear yes`), RTC set from the PC (`rtc set YYYY-MM-DD hh:mm:ss`),
dummy load in the bag, buzzer unmuted.

| # | Shot (PRD §12) | What to show | Say (key points) | Who |
|---|---|---|---|---|
| 1 | Power on → self-test | Press RESET: LCD self-test list (IMU, ALS/PS, AHT, RTC, SPI flash, LED matrix, SD absent, codec, Wi-Fi, model), summary line, chirp; console `[selftest]` lines | One firmware; every module checked at boot; SD card absent in this kit and handled | ____ |
| 2 | RUN: the four states | Bag on the desk → STILL; wear it and walk → CARRIED; set down hard (dummy load) → ROUGH banner + chirp; open and reach in → ACCESS. Close-ups of the LCD: state, confidence, "why" lines | The MLP runs on the STM32F407 every 0.5 s on a 2 s window; the "why" values are the features behind the decision | ____ |
| 3 | GUARD | Hold LEFT ~1 s (header GUARD, red matrix LED), open the bag → alarm screen, sound, red LED; press any key → stops | Worst error for the user is a missed ACCESS while guarded; acknowledge is one press | ____ |
| 4 | Climate warning | Lower the threshold for the demo (`climate set 30 70 10`) or breathe gently on the AHT21 (bottom-left corner): "Bag too hot/humid!"; restore with `climate set 40 80 30`. **No heat guns** | AHT21 is context only, a rule, not a model input | ____ |
| 5 | DIAG pages | UP, then RIGHT through Sensors (live values), Tasks (stack HWM, heap, CPU %), Inference (timing), System (RTC, flash, event log, Wi-Fi, watchdog), I/O test (DOWN: LEDs, matrix, buzzer) | Every verified module is live in the service mode; numbers are measured on the device | ____ |
| 6 | Offline | `wifi off` (or show the RW007 held in reset in DIAG System), keep using the bag for 5 minutes (cut the video, keep the LCD clock in view) | Nothing needs a network; Wi-Fi is not on the inference path | ____ |
| 7 | Controlled failure | `fault inject imu`: LCD "Sensor fault - IMU", red LED, windows rejected; `fault clear imu`: recovers. Optional: `fault hang infer` → watchdog reset, next boot names the task | Invalid data is rejected, not classified; other tasks keep running; the watchdog catches a hung task | ____ |
| 8 | Repo + build | GitHub repo page; `pio run -t clean`, `pio run`, `pio run -t upload` in the terminal; point at `docs/test_plan.md` and `evidence/` | Reproducible build from source; every number in the report comes from a log in `evidence/` | ____ |
| 9 | Results (slides or README) | Confusion matrix, model vs baseline, latency, memory, soak result | TODO(measure): only the real numbers from `ml/reports/` and the M6 analysis | ____ |

Checklist after filming: every shot recorded; nothing private in frame
(screens, names, Wi-Fi names); the link works when not signed in.
