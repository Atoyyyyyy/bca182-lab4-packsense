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

/* Task + console commands. Call before the scheduler starts.
   start_in_mode: enter CAPTURE mode as soon as the task runs. */
void capture_init(bool start_in_mode);

#endif /* CAPTURE_H */
