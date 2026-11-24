/*
 * selftest.c: `selftest model` (PRD 7.4, AC-3 / AC-4).
 *
 * For every golden window exported by ml/export_c.py: rebuild the window from
 * the stored integers (the same values the Python side used), compute the 42
 * features and the MLP output on the MCU, and compare with the Python
 * results. Prints the worst feature / probability errors, the argmax and
 * baseline agreement, and the DWT-timed feature and inference durations
 * (median, p95 over all windows).
 */
#define GOLDEN_DEFINE_TABLES
#include "selftest.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"

#include "app/cli.h"
#include "app/fmt.h"
#include "board_pins.h"
#include "drivers/console_uart.h"
#include "dsp/features.h"
#include "golden_vectors.h"
#include "model/model.h"

#define T_NOW() ((unsigned long)xTaskGetTickCount())

#if GOLDEN_N > 0
static feat_window_t s_w;
static uint32_t s_cyc_feat[GOLDEN_N], s_cyc_inf[GOLDEN_N];
static float s_feat_err[FEAT_N]; /* worst absolute error per feature */

static int cmp_u32(const void *a, const void *b)
{
    const uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

static void pct(uint32_t *v, unsigned n, uint32_t *med, uint32_t *p95, uint32_t *mx)
{
    qsort(v, n, sizeof v[0], cmp_u32);
    *med = v[n / 2];
    *p95 = v[(n * 95U + 99U) / 100U - 1U];
    *mx = v[n - 1];
}

static void load_window(unsigned k)
{
    const int16_t *imu = &golden_imu[k * GOLDEN_WIN * 6];
    for (unsigned i = 0; i < GOLDEN_WIN; i++) {
        s_w.ax[i] = (float)imu[i * 6 + 0] / 800.0f;
        s_w.ay[i] = (float)imu[i * 6 + 1] / 800.0f;
        s_w.az[i] = (float)imu[i * 6 + 2] / 800.0f;
        s_w.gx[i] = (float)imu[i * 6 + 3] / 60.0f;
        s_w.gy[i] = (float)imu[i * 6 + 4] / 60.0f;
        s_w.gz[i] = (float)imu[i * 6 + 5] / 60.0f;
        s_w.als[i] = (float)golden_als[k * GOLDEN_WIN + i] * 0.35f;
        const int16_t p = golden_ps[k * GOLDEN_WIN + i];
        s_w.ps_valid[i] = p >= 0 ? 1U : 0U;
        s_w.ps[i] = (float)(p >= 0 ? p : -(p + 1));
    }
}
#endif

static int cmd_selftest_model(int argc, char **argv)
{
    const bool verbose = (argc == 1 && argv[0][0] == 'v');
    if (argc > 1 || (argc == 1 && !verbose)) {
        return 1;
    }
    console_printf("[selftest] t=%lu model %s (version %s, trained %s), %d golden windows\r\n", T_NOW(),
                   model_available() ? "present" : "NOT exported yet", model_version(), MODEL_TRAINED, GOLDEN_N);
#if GOLDEN_N > 0
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    for (int i = 0; i < FEAT_N; i++) {
        s_feat_err[i] = 0.0f;
    }
    float max_feat_abs = 0.0f, max_feat_rel = 0.0f, max_prob = 0.0f;
    int worst_feat = 0;
    unsigned argmax_ok = 0, base_ok = 0, label_ok = 0;
    float feat[FEAT_N], prob[MODEL_N_CLASSES];
    for (unsigned k = 0; k < GOLDEN_N; k++) {
        load_window(k);
        uint32_t c0 = DWT->CYCCNT;
        features_compute(&s_w, feat);
        uint32_t c1 = DWT->CYCCNT;
        const int cls = model_infer(feat, prob);
        uint32_t c2 = DWT->CYCCNT;
        s_cyc_feat[k] = c1 - c0;
        s_cyc_inf[k] = c2 - c1;
        for (int i = 0; i < FEAT_N; i++) {
            const float ref = golden_feat[k * FEAT_N + i];
            const float e = fabsf(feat[i] - ref);
            const float rel = e / (fabsf(ref) > 1e-3f ? fabsf(ref) : 1e-3f);
            if (e > s_feat_err[i]) {
                s_feat_err[i] = e;
            }
            if (e > max_feat_abs) {
                max_feat_abs = e;
            }
            if (rel > max_feat_rel) {
                max_feat_rel = rel;
                worst_feat = i;
            }
        }
        int ref_cls = 0;
        for (int c = 0; c < MODEL_N_CLASSES; c++) {
            const float e = fabsf(prob[c] - golden_prob[k * MODEL_N_CLASSES + c]);
            if (e > max_prob) {
                max_prob = e;
            }
            if (golden_prob[k * MODEL_N_CLASSES + c] > golden_prob[k * MODEL_N_CLASSES + ref_cls]) {
                ref_cls = c;
            }
        }
        argmax_ok += (cls == ref_cls);
        label_ok += (cls == (int)golden_label[k]);
        base_ok += (baseline_classify(feat) == (int)golden_base[k]);
        if ((k & 7U) == 7U) {
            vTaskDelay(1); /* let lower-priority tasks run */
        }
    }
    uint32_t fm, f95, fmx, im, i95, imx;
    pct(s_cyc_feat, GOLDEN_N, &fm, &f95, &fmx);
    pct(s_cyc_inf, GOLDEN_N, &im, &i95, &imx);
    const uint32_t mhz = SystemCoreClock / 1000000U;
    char e1[FMT_F_BUF], e2[FMT_F_BUF], e3[FMT_F_BUF];
    console_printf("[selftest] features: max abs err %s, max rel err %s (worst: %s)\r\n",
                   fmt_e(e1, sizeof e1, max_feat_abs), fmt_e(e2, sizeof e2, max_feat_rel),
                   model_feature_name(worst_feat));
    console_printf("[selftest] MLP: max prob err %s, argmax match %u/%u (%u.%u %%), baseline match %u/%u, "
                   "true-label agreement %u/%u\r\n",
                   fmt_e(e3, sizeof e3, max_prob), argmax_ok, (unsigned)GOLDEN_N, argmax_ok * 100U / GOLDEN_N,
                   (argmax_ok * 1000U / GOLDEN_N) % 10U, base_ok, (unsigned)GOLDEN_N, label_ok, (unsigned)GOLDEN_N);
    console_printf("[selftest] time over %u runs: features median %lu us p95 %lu us max %lu us; "
                   "inference median %lu us p95 %lu us max %lu us\r\n",
                   (unsigned)GOLDEN_N, (unsigned long)(fm / mhz), (unsigned long)(f95 / mhz),
                   (unsigned long)(fmx / mhz), (unsigned long)(im / mhz), (unsigned long)(i95 / mhz),
                   (unsigned long)(imx / mhz));
    console_printf("[selftest] AC-3 (argmax match >= 98 %%): %s\r\n",
                   argmax_ok * 100U >= 98U * GOLDEN_N ? "PASS" : "FAIL");
    if (verbose) {
        for (int i = 0; i < FEAT_N; i++) {
            console_printf("[selftest]   %2d %-16s max abs err %s\r\n", i, model_feature_name(i),
                           fmt_e(e1, sizeof e1, s_feat_err[i]));
        }
    }
#else
    (void)verbose;
    console_printf("[selftest] no golden vectors: run ml/prepare.py, train.py, export_c.py, then rebuild\r\n");
#endif
    return 0;
}

static const cli_cmd_t s_cmds[] = {
    {"selftest model", "[v]", "golden vectors: MCU features + MLP vs Python (AC-3), timing", cmd_selftest_model},
};

void selftest_cmds_register(void)
{
    cli_register(s_cmds, sizeof s_cmds / sizeof s_cmds[0]);
}
