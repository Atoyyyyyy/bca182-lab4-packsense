/*
 * decision.c: majority-of-3 smoothing (see decision.h). Used by InferTask only.
 */
#include "decision.h"

#include "app_config.h"

#define HIST 3

static int s_cls[HIST];
static float s_prob[HIST][MODEL_N_CLASSES];
static unsigned s_n;      /* valid entries (<= HIST), newest at s_cls[(s_head + HIST - 1) % HIST] */
static unsigned s_head;   /* next slot */
static int s_state = DECISION_NONE;

void decision_reset(void)
{
    s_n = 0U;
    s_head = 0U;
    s_state = DECISION_NONE;
}

decision_t decision_update(int cls, const float prob[MODEL_N_CLASSES])
{
    s_cls[s_head] = cls;
    for (int c = 0; c < MODEL_N_CLASSES; c++) {
        s_prob[s_head][c] = prob[c];
    }
    s_head = (s_head + 1U) % HIST;
    if (s_n < HIST) {
        s_n++;
    }

    int best = cls, best_n = 1;
    for (unsigned i = 0; i < s_n; i++) {
        int n = 0;
        for (unsigned j = 0; j < s_n; j++) {
            n += (s_cls[j] == s_cls[i]);
        }
        if (n > best_n) {
            best_n = n;
            best = s_cls[i];
        }
    }
    float conf = 0.0f;
    for (unsigned i = 0; i < s_n; i++) {
        conf += s_prob[i][best];
    }
    conf /= (float)s_n;

    decision_t d;
    d.changed = best != s_state;
    d.state = best;
    d.conf = conf;
    d.uncertain = conf < DECISION_MIN_CONF;
    s_state = best;
    return d;
}

decision_t decision_invalid(void)
{
    s_n = 0U;
    s_head = 0U;
    decision_t d = {s_state, 0.0f, true, false};
    return d;
}
