/*
 * app_mode.h: operating modes (PRD 5.1).
 *
 *   BOOT     power-on self-test, summary on the LCD for 2 s
 *   RUN      classify, alert on ROUGH, log
 *   GUARD    RUN + ACCESS sounds the alarm until a key acknowledges it
 *   CAPTURE  labelled trials to the flash log (capture.c; hold UP at RESET)
 *   DIAG     service pages (live sensors, tasks, inference, system, I/O test)
 *
 * The UI task owns the mode: it is the only caller of app_mode_switch(), and
 * console commands (`mode ...`, `capture on|off`) go through
 * app_mode_request(), which the UI task carries out. Switching hands the
 * sensor queue, keys, LCD and LEDs from the product tasks (InferTask/UiTask)
 * to the capture task and back, one owner at a time.
 */
#ifndef APP_MODE_H
#define APP_MODE_H

#include <stdbool.h>

typedef enum { MODE_BOOT = 0, MODE_RUN, MODE_GUARD, MODE_CAPTURE, MODE_DIAG, MODE_COUNT } app_mode_t;

const char *app_mode_name(app_mode_t m);
app_mode_t app_mode_get(void);
bool app_mode_is_product(app_mode_t m); /* RUN, GUARD, DIAG: inference running */

/* From any task but the UI task: ask for a mode, wait until it is done
   (<= 10 s). Returns the mode that is active afterwards. */
app_mode_t app_mode_request(app_mode_t m);

/* UI task only. */
void app_mode_init(void);
bool app_mode_take_request(app_mode_t *m);
void app_mode_request_done(void);
app_mode_t app_mode_switch(app_mode_t to);

/* `mode` console command. Before the scheduler. */
void app_mode_cmds_register(void);

#endif /* APP_MODE_H */
