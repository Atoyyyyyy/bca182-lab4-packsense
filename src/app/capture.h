/*
 * capture.h: CAPTURE mode (PRD 5.1, 7.1; M3).
 *
 * Labelled trials are recorded to the SPI flash log (caplog.h) so the bag can
 * be carried without a cable (board powered from a power bank). On the board:
 *   LEFT / RIGHT  choose the label (beeps 1..4 times = STILL, CARRIED, ROUGH,
 *                 ACCESS; LED matrix + LCD colour = blue, green, orange, magenta)
 *   UP            start a trial: 5 s countdown (ticks), long beep, recording
 *                 (red LED on, matrix shows the time left); stops by itself
 *                 after the label's duration with a double beep
 *   DOWN          stop early / cancel the countdown
 * Enter the mode by holding UP while pressing RESET (or `capture on`).
 * tools/capture.py downloads the log as data/raw/<session>.csv.
 */
#ifndef CAPTURE_H
#define CAPTURE_H

#include <stdbool.h>

/* Task + console commands. Call before the scheduler starts. */
void capture_init(void);

/* Enter / leave the mode (from the UI task, which owns mode changes; see
   app_mode.h). Waits until done. Returns true if the mode is now on. */
bool capture_set(bool on);
bool capture_is_on(void);

#endif /* CAPTURE_H */
