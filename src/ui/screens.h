/*
 * screens.h: LCD screens (boot self-test, home, guard alarm, DIAG pages).
 * Drawn by the UI task only. Lines are redrawn in place with their own
 * background (no clear first), so updates do not flicker.
 */
#ifndef SCREENS_H
#define SCREENS_H

#include <stdbool.h>
#include <stdint.h>

#include "app/app_mode.h"
#include "app/infer_task.h"

#define SCR_DIAG_PAGES 5U

typedef struct {
    app_mode_t mode;
    bool muted;
    bool have_result;
    infer_result_t r;        /* latest window */
    bool climate_warn;
    const char *banner;      /* NULL = none (fault line shown instead, if any) */
    uint16_t banner_bg;
    uint32_t fault;          /* sensor/gap faults of the latest window */
    char clock[12];          /* "hh:mm:ss" or "--:--:--" */
    int key;                 /* button_id_t being echoed in the footer, -1 = none */
} ui_view_t;

void scr_boot_title(void);
void scr_boot_row(unsigned row, const char *name, const char *result, uint16_t color);
void scr_boot_summary(unsigned ok, unsigned absent, unsigned fail, uint32_t ms);

void scr_home(const ui_view_t *v, bool full);
void scr_alarm(bool phase);
void scr_diag(unsigned page, bool full, const char *action_msg);

uint16_t scr_state_color(int state);
void scr_fault_text(uint32_t fault, char *buf, unsigned n);

#endif /* SCREENS_H */
