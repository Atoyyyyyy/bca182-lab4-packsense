/*
 * infer_task.h: InferTask (PRD 6.3): window buffer, validation, features,
 * MLP, smoothing. Priority 4, woken by every sample; a window of 200 samples
 * (2 s) is processed every 50 new samples (0.5 s hop).
 *
 * Samples are stored exactly as the capture log stores them (caplog_pack),
 * so the live features see the same quantisation as the training data.
 * A window is not classified when more than INFER_MAX_INVALID samples are
 * invalid (IMU error, gap, a run of >= 3 saturated samples; ALS errors are
 * counted separately) or its span shows a gap (PRD 6.4); the decision then
 * keeps the last state, marked uncertain.
 */
#ifndef INFER_TASK_H
#define INFER_TASK_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"

#include "app/decision.h"
#include "app/sensor_task.h"
#include "model/model.h"

/* infer_result_t.fault bits: sensor_id_t bits, plus: */
#define INFER_FAULT_GAP  0x08U  /* samples missing in the window */

typedef enum { WHY_MOTION_G = 0, WHY_JERK, WHY_LUX, WHY_PROX, WHY_TILT, WHY_N } why_t;

typedef struct {
    uint32_t seq;            /* window number since start */
    uint32_t t_end_ms;       /* time stamp of the window's last sample */
    uint32_t t_done_ms;      /* decision ready */
    bool valid;              /* enough valid samples: features computed */
    bool classified;         /* valid and a model is available */
    int raw;                 /* this window's MLP argmax, -1 if not classified */
    int base;                /* baseline class, -1 if not classified */
    float prob[MODEL_N_CLASSES];
    decision_t dec;          /* smoothed state */
    uint32_t fault;          /* why the window was rejected / degraded sensors */
    uint16_t imu_bad, als_bad;
    uint32_t t_feat_us, t_inf_us;
    float why[WHY_N];        /* motion sd (g), jerk peak (m/s^2), light (lux), prox max, tilt (deg) */
    float temp, rh;          /* latest AHT values */
    bool env_ok;
} infer_result_t;

typedef struct {
    uint32_t n;                         /* timed windows in the stats (<= 128 most recent) */
    uint32_t feat_med, feat_p95, feat_max; /* us */
    uint32_t inf_med, inf_p95, inf_max;
} infer_timing_t;

typedef struct {
    bool active;
    uint32_t windows, valid, classified, rejected;
    uint32_t rejected_imu, rejected_als, rejected_gap;
    uint32_t per_state[MODEL_N_CLASSES];  /* smoothed decisions */
    uint32_t results_dropped;             /* UI queue full */
    infer_timing_t timing;
} infer_stats_t;

void infer_task_create(void);

/* Start/stop sampling + inference (product modes). Waits until applied. */
void infer_set_active(bool on);
bool infer_is_active(void);

/* Results for the UI task (one per window). */
QueueHandle_t infer_results(void);

void infer_get_last(infer_result_t *out);
void infer_get_last_sample(sample_t *out, bool *have);
void infer_get_stats(infer_stats_t *out);

/* Per-window console line ([win] ..., printed by the UI after feedback). */
bool infer_log_enabled(void);

#endif /* INFER_TASK_H */
