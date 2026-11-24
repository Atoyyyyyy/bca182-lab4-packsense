/*
 * infer_task.c: InferTask (see infer_task.h).
 */
#include "infer_task.h"

#include <math.h>
#include <string.h>

#include "stm32f4xx.h"

#include "task.h"

#include "app/caplog.h"
#include "app/cli.h"
#include "app/fmt.h"
#include "app/wdg.h"
#include "app_config.h"
#include "drivers/console_uart.h"
#include "dsp/features.h"

#define HOP        50U
#define SPAN_MAX   ((FEAT_WIN - 1U) * SENSOR_PERIOD_MS + 50U) /* ml/prepare.py */
#define SAT_RUN    3U
#define TIMING_N   128U

static CCM_BSS StackType_t s_stack[INFER_TASK_STACK_WORDS];
static CCM_BSS StaticTask_t s_tcb;
static CCM_BSS caplog_rec_t s_ring[FEAT_WIN];
static CCM_BSS feat_window_t s_w;
static float s_feat[FEAT_N];
static uint8_t s_bad[FEAT_WIN];

static StaticQueue_t s_res_buf;
static uint8_t s_res_storage[INFER_RESULT_QUEUE_LEN * sizeof(infer_result_t)];
static QueueHandle_t s_res;

static volatile bool s_req_active;
static volatile bool s_active;
static volatile bool s_log = true;
static unsigned s_head, s_count, s_since;
static uint32_t s_seq;

static infer_result_t s_last;
static sample_t s_last_sample;
static bool s_have_sample;
static infer_stats_t s_stats;
static uint32_t s_t_feat[TIMING_N], s_t_inf[TIMING_N];
static unsigned s_t_n, s_t_head;

static void timing_add(uint32_t feat_us, uint32_t inf_us)
{
    s_t_feat[s_t_head] = feat_us;
    s_t_inf[s_t_head] = inf_us;
    s_t_head = (s_t_head + 1U) % TIMING_N;
    if (s_t_n < TIMING_N) {
        s_t_n++;
    }
}

static void pct(const uint32_t *src, unsigned n, uint32_t *med, uint32_t *p95, uint32_t *mx)
{
    static uint32_t v[TIMING_N];
    memcpy(v, src, n * sizeof v[0]);
    for (unsigned i = 1; i < n; i++) { /* insertion sort, n <= 128 */
        const uint32_t x = v[i];
        unsigned j = i;
        while (j > 0U && v[j - 1U] > x) {
            v[j] = v[j - 1U];
            j--;
        }
        v[j] = x;
    }
    *med = v[n / 2U];
    *p95 = v[(n * 95U + 99U) / 100U - 1U];
    *mx = v[n - 1U];
}

static void timing_update(void)
{
    infer_timing_t t = {0};
    t.n = s_t_n;
    if (s_t_n > 0U) {
        pct(s_t_feat, s_t_n, &t.feat_med, &t.feat_p95, &t.feat_max);
        pct(s_t_inf, s_t_n, &t.inf_med, &t.inf_p95, &t.inf_max);
    }
    taskENTER_CRITICAL();
    s_stats.timing = t;
    taskEXIT_CRITICAL();
}

/* Oldest -> newest into s_w; mark invalid samples as ml/prepare.py does. */
static void build_window(infer_result_t *r)
{
    unsigned run = 0U;
    uint16_t als_bad = 0U;
    for (unsigned i = 0; i < FEAT_WIN; i++) {
        const caplog_rec_t *q = &s_ring[(s_head + i) % FEAT_WIN];
        s_w.ax[i] = (float)q->a[0] / CAPLOG_ACC_SCALE;
        s_w.ay[i] = (float)q->a[1] / CAPLOG_ACC_SCALE;
        s_w.az[i] = (float)q->a[2] / CAPLOG_ACC_SCALE;
        s_w.gx[i] = (float)q->g[0] / CAPLOG_GYR_SCALE;
        s_w.gy[i] = (float)q->g[1] / CAPLOG_GYR_SCALE;
        s_w.gz[i] = (float)q->g[2] / CAPLOG_GYR_SCALE;
        s_w.als[i] = (float)q->als * CAPLOG_ALS_LSB;
        s_w.ps[i] = (float)q->ps;
        s_w.ps_valid[i] = (q->flags & CAPLOG_FLAG_PS_VALID) ? 1U : 0U;
        s_bad[i] = (q->flags & (SF_IMU_ERR | SF_GAP)) ? 1U : 0U;
        if (q->flags & SF_IMU_SAT) {
            run++;
            if (run >= SAT_RUN) {
                for (unsigned k = i + 1U - run; k <= i; k++) {
                    s_bad[k] = 1U;
                }
            }
        } else {
            run = 0U;
        }
        if (q->flags & SF_ALS_ERR) {
            als_bad++;
        }
    }
    uint16_t imu_bad = 0U;
    for (unsigned i = 0; i < FEAT_WIN; i++) {
        imu_bad = (uint16_t)(imu_bad + s_bad[i]);
    }
    const uint32_t t0 = s_ring[s_head].t_ms;
    const uint32_t t1 = s_ring[(s_head + FEAT_WIN - 1U) % FEAT_WIN].t_ms;
    r->t_end_ms = t1;
    r->imu_bad = imu_bad;
    r->als_bad = als_bad;
    r->fault = sensors_fault_mask();
    if (imu_bad > INFER_MAX_INVALID) {
        r->fault |= 1UL << SENS_IMU;
    }
    if (als_bad > INFER_MAX_INVALID) {
        r->fault |= 1UL << SENS_ALS;
    }
    if ((t1 - t0) > SPAN_MAX) {
        r->fault |= INFER_FAULT_GAP;
    }
}

static void process_window(void)
{
    infer_result_t r;
    memset(&r, 0, sizeof r);
    r.seq = ++s_seq;
    r.raw = -1;
    r.base = -1;
    build_window(&r);
    r.valid = (r.fault & ((1UL << SENS_IMU) | (1UL << SENS_ALS) | INFER_FAULT_GAP)) == 0U;
    s_stats.windows++;

    if (r.valid) {
        s_stats.valid++;
        const uint32_t c0 = DWT->CYCCNT;
        features_compute(&s_w, s_feat);
        const uint32_t c1 = DWT->CYCCNT;
        int cls = -1;
        if (model_available()) {
            cls = model_infer(s_feat, r.prob);
        }
        const uint32_t c2 = DWT->CYCCNT;
        const uint32_t mhz = SystemCoreClock / 1000000U;
        r.t_feat_us = (c1 - c0) / mhz;
        r.t_inf_us = (c2 - c1) / mhz;
        r.why[WHY_MOTION_G] = s_feat[FI_AMAG_STD] / FEAT_G;
        r.why[WHY_JERK] = s_feat[FI_JERK_PEAK];
        r.why[WHY_LUX] = powf(10.0f, s_feat[FI_ALS_LOGMEAN]) - 1.0f;
        r.why[WHY_PROX] = s_feat[FI_PS_MAX];
        r.why[WHY_TILT] = s_feat[FI_TILT];
        if (cls >= 0) {
            r.classified = true;
            r.raw = cls;
            r.base = baseline_classify(s_feat);
            r.dec = decision_update(cls, r.prob);
            s_stats.classified++;
            s_stats.per_state[r.dec.state]++;
            timing_add(r.t_feat_us, r.t_inf_us);
        } else {
            r.dec.state = DECISION_NONE;
            timing_add(r.t_feat_us, 0U);
        }
    } else {
        s_stats.rejected++;
        s_stats.rejected_imu += (r.fault & (1UL << SENS_IMU)) ? 1U : 0U;
        s_stats.rejected_als += (r.fault & (1UL << SENS_ALS)) ? 1U : 0U;
        s_stats.rejected_gap += (r.fault & INFER_FAULT_GAP) ? 1U : 0U;
        r.dec = decision_invalid();
    }
    const caplog_rec_t *last = &s_ring[(s_head + FEAT_WIN - 1U) % FEAT_WIN];
    r.temp = (float)last->temp / 100.0f;
    r.rh = (float)last->rh / 100.0f;
    r.env_ok = (last->flags & SF_ENV_ERR) == 0U;
    r.t_done_ms = (uint32_t)xTaskGetTickCount();

    taskENTER_CRITICAL();
    s_last = r;
    taskEXIT_CRITICAL();
    if (xQueueSend(s_res, &r, 0) != pdPASS) {
        s_stats.results_dropped++;
    }
    if ((s_seq % 4U) == 0U) {
        timing_update();
    }
}

static void apply_request(void)
{
    if (s_req_active == s_active) {
        return;
    }
    if (s_req_active) {
        s_head = s_count = s_since = 0U;
        s_seq = 0U;
        decision_reset();
        sensors_start_any();
        s_active = true;
        s_stats.active = true;
    } else {
        sensors_stop();
        s_active = false;
        s_stats.active = false;
        taskENTER_CRITICAL();
        s_have_sample = false;
        taskEXIT_CRITICAL();
    }
}

static void infer_task(void *arg)
{
    (void)arg;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    for (;;) {
        wdg_checkin(WDG_INFER);
        apply_request();
        if (!s_active) {
            (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(200));
            continue;
        }
        sample_t s;
        if (xQueueReceive(sensors_queue(), &s, pdMS_TO_TICKS(200)) != pdPASS) {
            continue;
        }
        caplog_pack(&s, 0U, 0U, 0U, &s_ring[s_head]);
        s_head = (s_head + 1U) % FEAT_WIN;
        if (s_count < FEAT_WIN) {
            s_count++;
        }
        s_since++;
        taskENTER_CRITICAL();
        s_last_sample = s;
        s_have_sample = true;
        taskEXIT_CRITICAL();
        if (s_count == FEAT_WIN && s_since >= HOP) {
            s_since = 0U;
            process_window();
        }
    }
}

/* ---- API ------------------------------------------------------------------ */

static TaskHandle_t s_task;

void infer_set_active(bool on)
{
    s_req_active = on;
    xTaskNotifyGive(s_task);
    for (unsigned i = 0; i < 300U && s_active != on; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

bool infer_is_active(void)
{
    return s_active;
}

QueueHandle_t infer_results(void)
{
    return s_res;
}

void infer_get_last(infer_result_t *out)
{
    taskENTER_CRITICAL();
    *out = s_last;
    taskEXIT_CRITICAL();
}

void infer_get_last_sample(sample_t *out, bool *have)
{
    taskENTER_CRITICAL();
    *out = s_last_sample;
    *have = s_have_sample;
    taskEXIT_CRITICAL();
}

void infer_get_stats(infer_stats_t *out)
{
    taskENTER_CRITICAL();
    *out = s_stats;
    taskEXIT_CRITICAL();
}

bool infer_log_enabled(void)
{
    return s_log;
}

/* ---- console ---------------------------------------------------------------- */

static int cmd_infer(int argc, char **argv)
{
    (void)argv;
    if (argc != 0) {
        return 1;
    }
    infer_stats_t st;
    infer_get_stats(&st);
    infer_result_t r;
    infer_get_last(&r);
    console_printf("[infer] t=%lu %s, model %s (%s), windows %lu: valid %lu, classified %lu, rejected %lu "
                   "(imu %lu, als %lu, gap %lu), results dropped %lu\r\n",
                   (unsigned long)xTaskGetTickCount(), st.active ? "active" : "stopped",
                   model_available() ? "present" : "none", model_version(), (unsigned long)st.windows,
                   (unsigned long)st.valid, (unsigned long)st.classified, (unsigned long)st.rejected,
                   (unsigned long)st.rejected_imu, (unsigned long)st.rejected_als, (unsigned long)st.rejected_gap,
                   (unsigned long)st.results_dropped);
    console_printf("[infer] decisions: STILL %lu, CARRIED %lu, ROUGH %lu, ACCESS %lu\r\n",
                   (unsigned long)st.per_state[0], (unsigned long)st.per_state[1], (unsigned long)st.per_state[2],
                   (unsigned long)st.per_state[3]);
    console_printf("[infer] timing over the last %lu windows: features median %lu us p95 %lu us max %lu us; "
                   "MLP median %lu us p95 %lu us max %lu us\r\n",
                   (unsigned long)st.timing.n, (unsigned long)st.timing.feat_med, (unsigned long)st.timing.feat_p95,
                   (unsigned long)st.timing.feat_max, (unsigned long)st.timing.inf_med,
                   (unsigned long)st.timing.inf_p95, (unsigned long)st.timing.inf_max);
    if (r.seq > 0U) {
        char b[5][FMT_F_BUF];
        console_printf("[infer] last window %lu: %s, why: motion sd %s g, jerk %s m/s2, light %s lux, prox %s, "
                       "tilt %s deg\r\n",
                       (unsigned long)r.seq, r.valid ? "valid" : "REJECTED",
                       fmt_f(b[0], sizeof b[0], r.why[WHY_MOTION_G], 3), fmt_f(b[1], sizeof b[1], r.why[WHY_JERK], 2),
                       fmt_f(b[2], sizeof b[2], r.why[WHY_LUX], 1), fmt_f(b[3], sizeof b[3], r.why[WHY_PROX], 0),
                       fmt_f(b[4], sizeof b[4], r.why[WHY_TILT], 1));
    }
    return 0;
}

static int cmd_infer_log(int argc, char **argv)
{
    if (argc != 1) {
        return 1;
    }
    if (strcmp(argv[0], "on") == 0) {
        s_log = true;
    } else if (strcmp(argv[0], "off") == 0) {
        s_log = false;
    } else {
        return 1;
    }
    return 0;
}

static const cli_cmd_t s_cmds[] = {
    {"infer", "", "inference status, decision counts, feature/MLP timing (AC-4)", cmd_infer},
    {"infer log", "on|off", "one [win] line per window (default on)", cmd_infer_log},
};

void infer_task_create(void)
{
    s_res = xQueueCreateStatic(INFER_RESULT_QUEUE_LEN, sizeof(infer_result_t), s_res_storage, &s_res_buf);
    s_task = xTaskCreateStatic(infer_task, "Infer", INFER_TASK_STACK_WORDS, NULL, INFER_TASK_PRIO, s_stack, &s_tcb);
    configASSERT(s_res != NULL && s_task != NULL);
    cli_register(s_cmds, sizeof s_cmds / sizeof s_cmds[0]);
}
