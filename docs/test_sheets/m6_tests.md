# M6 test sheet: running T1–T9

**Who:** H, ideally two or three people (one carries the laptop on the walking
steps). **Takes:** about 1.5 hours, can be split over two sittings.
**What is tested and how it is scored:** [`docs/test_plan.md`](../test_plan.md).

## Before you start

1. M3 and M4 are done: the agent has trained and exported the real model, and
   `evidence/m5_parity.log` passed (`AC-3 ... PASS`). The M6 tests mean nothing
   with the placeholder model (the LCD would say `NO MODEL`).
2. `git status` is clean, so the firmware banner has no `-dirty`. The first
   command below uses `--flash` to build and upload the committed firmware.
3. Have ready: the bag(s) B1 / B2 from the recordings, the **dummy load**
   (books, a water bottle; never a laptop in ROUGH steps), a cushion or bed for
   tosses, a chair, the laptop with a USB cable long enough to reach the bag.
4. Board in the bag exactly as in the recordings: flat against the back panel,
   LCD facing into the compartment. The cable leaves through one corner of the
   zipper; close the zipper up to the cable.
5. Close every other serial monitor. Each command below says what to do on the
   screen; answer the questions with what you actually saw.

All commands are for PowerShell. Copy one line at a time.

## T3 memory (2 minutes, no board needed)

```powershell
cd "$env:USERPROFILE\Documents\BCA182 - LAB 4"
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -t clean
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -v > evidence\size.txt
```

## T7 input control (5 minutes, board on the desk)

Flashes the committed firmware first (only needed once):

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" "$env:USERPROFILE\Documents\BCA182 - LAB 4\tools\capture_m2.py" m6_keys --flash
```

Ten alarm acknowledges, ten arm / disarm holds, ten mute / unmute holds, ten
DIAG in / out presses. Do exactly one action per line, then wait.

## T8 diagnostics (4 minutes, board on the desk)

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" "$env:USERPROFILE\Documents\BCA182 - LAB 4\tools\capture_m2.py" m6_diag
```

Walk the five DIAG pages with the keys when told. Film the LCD with a phone
if you can.

## T6 controlled failure (5 minutes, board on the desk, one RESET press)

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" "$env:USERPROFILE\Documents\BCA182 - LAB 4\tools\capture_m2.py" m6_fault
```

Simulated IMU / light / temperature sensor failures, Wi-Fi off and on, one
RESET while it classifies, then a deliberate hang that the watchdog must catch
(the board resets by itself, about 8 s later).

## T1 functional per class (about 45 minutes, four runs)

One run per class, 20 trials each, from [`T1.csv`](T1.csv). Before each
trial the screen shows the set-up (bag, load, who, where, light); press
Enter when it is set up, do the action until STOP, then press Enter again
(or type `x` if the trial went wrong: someone bumped the bag, wrong set-up).

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" "$env:USERPROFILE\Documents\BCA182 - LAB 4\tools\capture_m2.py" m6_t1_still
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" "$env:USERPROFILE\Documents\BCA182 - LAB 4\tools\capture_m2.py" m6_t1_carried
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" "$env:USERPROFILE\Documents\BCA182 - LAB 4\tools\capture_m2.py" m6_t1_rough
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" "$env:USERPROFILE\Documents\BCA182 - LAB 4\tools\capture_m2.py" m6_t1_access
```

- STILL (12 s per trial): put the bag where the line says, press Enter, step
  back. Nobody touches the bag or the table until STOP.
- CARRIED (20 s): bag on as the line says, press Enter, walk normally. Level
  ground only, no stairs or running while the cable is attached.
- ROUGH (8 s): **dummy load only**, no real drops, tosses ≤ 50 cm onto a
  cushion or bed.
- ACCESS (12 s): open, reach in, move items, close before STOP.

Bag B2, operator P3, a dim room: use what you have. If something on a line is
not possible (for example no second bag), do the nearest thing and type `x`
plus a short note only if the trial itself went wrong; a changed set-up is
fine, write it on paper and tell the agent.

## T4 soak (17 minutes)

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" "$env:USERPROFILE\Documents\BCA182 - LAB 4\tools\capture_m2.py" m6_soak
```

After the first Enter, the timed lines run back to back for 16 minutes: rest,
walking, two rough handlings, two openings (the second one in GUARD: the alarm
sounds, press any key once), DIAG pages, mute. Keep the dummy load in the bag.

## T5 offline (6 minutes, plus the optional part B)

**Part A**, Wi-Fi module switched off, same bag:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" "$env:USERPROFILE\Documents\BCA182 - LAB 4\tools\capture_m2.py" m6_offline
```

Film the LCD for the whole run if you can.

**Part B (optional):** unplug the laptop, plug the board into the power bank
(it restarts), leave it in the bag and use the bag for at least 5 minutes
(rest, walk, open it once, set it down hard once with the dummy load). Note the
start and end time on paper. Then plug the board back into the laptop (it
restarts again) and save its event log:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" "$env:USERPROFILE\Documents\BCA182 - LAB 4\tools\capture_m2.py" m6_eventlog
```

## T9 checklist (15 minutes, together)

Go through [`T9_checklist.md`](T9_checklist.md), tick each item you checked
on the board, fill in the blanks, sign.

## Videos

Videos stay out of git (size). Upload them (Drive, YouTube unlisted) and list
the links in `evidence/video_links.md`, one line per video: test, date, link.

## When you are done

Tell the agent "M6 evidence is in". It runs the analysis scripts on the
`evidence/m6_*` files and fills in the results.
