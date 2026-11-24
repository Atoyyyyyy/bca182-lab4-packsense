/*
 * baseline.c: the threshold rule baseline (ml/baseline.py), with thresholds
 * tuned on the training sessions and exported into model_params.h.
 */
#include "model/model.h"

int baseline_classify(const float feat[FEAT_N])
{
    if (feat[FI_AMAG_STD] < BASELINE_T_STILL) {
        return CLS_STILL;
    }
    if (feat[FI_JERK_PEAK] > BASELINE_T_ROUGH) {
        return CLS_ROUGH;
    }
    if (feat[FI_ALS_LOGRANGE] > BASELINE_T_ALS || feat[FI_PS_NEAR] > BASELINE_T_PS) {
        return CLS_ACCESS;
    }
    return CLS_CARRIED;
}
