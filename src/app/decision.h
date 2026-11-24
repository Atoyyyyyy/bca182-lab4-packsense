/*
 * decision.h: smoothing of the per-window predictions into the shown state.
 *
 * Majority of the last 3 classified windows (ml/evaluate.py `smooth3`, which
 * is what the reported mlp_smooth3 numbers measure); without a majority the
 * newest prediction is used. Confidence = mean probability of the chosen
 * class over those windows; below DECISION_MIN_CONF the state is shown as
 * uncertain. An unclassifiable window (sensor fault) keeps the last state,
 * marked uncertain, and restarts the history.
 */
#ifndef DECISION_H
#define DECISION_H

#include <stdbool.h>
#include <stdint.h>

#include "model/model.h"

#define DECISION_NONE (-1)

typedef struct {
    int state;        /* CLS_* or DECISION_NONE before the first window */
    float conf;       /* 0..1 */
    bool uncertain;
    bool changed;     /* state differs from the previous decision */
} decision_t;

void decision_reset(void);
decision_t decision_update(int cls, const float prob[MODEL_N_CLASSES]);
decision_t decision_invalid(void);

#endif /* DECISION_H */
