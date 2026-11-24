# M3 test sheet: recording a data session

**Who:** H (one or two people, a backpack, a USB power bank, a dummy load).
**Firmware:** `0.2.0-m2` or later from a clean tree (CAPTURE mode, M3).
**Result files:** `data/raw/S<nn>.csv`, `data/manifest.csv` (from `tools/capture.py dump`).

Labels, schema and the reasons behind the plan: [`data/README.md`](../../data/README.md).

## Before the first session (once)

1. Pick codes: operators P1, P2; bags B1, B2; locations L1, L2. Fill in the
   code table in `data/README.md` (descriptions only, no names).
2. Close any serial monitor. Check the board's log is empty:
   ```powershell
   & "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" "$env:USERPROFILE\Documents\BCA182 - LAB 4\tools\capture.py" status
   ```
   `log: 0 of 258048 records` means empty.

## One session (about 15 minutes)

Same bag, same carrier, same load and room for the whole session.

1. Put the board in the laptop compartment, **flat against the back panel,
   LCD side facing into the compartment** (towards where a hand comes in),
   always the same way. Plug the USB cable from the board's ST-LINK USB port
   into a power bank and put the power bank in the bag.
2. Hold **UP** and press **RESET**. Release UP after the beep: the board is in
   CAPTURE mode (LEDs light up blue = STILL, 1 beep).
3. Record these 14 trials. For each: choose the label with LEFT / RIGHT
   (count the beeps), press **UP**, close the bag during the 5 s countdown,
   and start the activity at the **long beep**. The trial ends by itself
   (double beep).

   | # | Label | Beeps | Do (whole trial) |
   |---|---|---|---|
   | 1–3 | STILL | 1 | bag closed, untouched: trial 1 floor, 2 desk, 3 chair |
   | 4–6 | CARRIED | 2 | walk normally: 4 and 5 worn on the back, 6 held by the handle |
   | 7–10 | ROUGH | 3 | keep it up for 15 s: 7 set down hard, 8 swing, 9 shake, 10 toss onto a cushion/bed (≤ 50 cm). **Dummy load only** |
   | 11–14 | ACCESS | 4 | bag on a surface: open, reach in, rummage; close when ~3 LEDs are left |

   A trial went wrong (wrong label, someone bumped the bag)? Press **DOWN**
   during it, and say so in the session notes when you download; it is
   removed in `ml/prepare.py` by trial number.
4. Write down on paper: session order, bag, load, room, anything odd.

## Download (after one or more sessions)

Unplug the power bank, plug the board into the PC (it restarts; the data
stays in the flash), close any serial monitor, then:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" "$env:USERPROFILE\Documents\BCA182 - LAB 4\tools\capture.py" dump
```

It shows each session found on the board and asks for the operator, bag,
load (`empty` / `loaded`), location codes and notes (no names). When it says
`dump OK`, erase the board for the next sessions:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" "$env:USERPROFILE\Documents\BCA182 - LAB 4\tools\capture.py" wipe
```

Then tell the agent "data is in"; it runs `ml/prepare.py` and reports the
window counts against the gate.

## Session plan (4 sessions minimum)

| Session | Carrier | Bag | Load | Room | Used for |
|---|---|---|---|---|---|
| 1 | P1 | B1 | empty | bright | train |
| 2 | P2 | B1 | loaded | dim | train |
| 3 | P1 | B2 | loaded | bright | train |
| 4 | P2 | B2 | empty | dim | **test** (held out) |

Only one bag or one person? Still record 4 sessions and vary what you can
(load, room, surfaces); note it, it becomes a stated limitation.
