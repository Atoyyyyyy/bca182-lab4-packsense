/*
 * mlp_infer.c: MLP inference (dense layers, ReLU, softmax).
 * The only file that instantiates the generated model tables.
 */
#define MODEL_DEFINE_TABLES
#include "model.h"

#include <math.h>

#include "model/model_weights.h"

_Static_assert(MODEL_N_FEATURES == FEAT_N, "model_params.h does not match dsp/features.h");

bool model_available(void)
{
    return MODEL_AVAILABLE != 0 && MLP_N_LAYERS > 0;
}

const char *model_version(void)
{
    return MODEL_VERSION;
}

const char *model_class_name(int c)
{
    return (c >= 0 && c < MODEL_N_CLASSES) ? model_class_names[c] : "?";
}

const char *model_feature_name(int i)
{
    return (i >= 0 && i < MODEL_N_FEATURES) ? model_feature_names[i] : "?";
}

int model_infer(const float feat[FEAT_N], float prob[MODEL_N_CLASSES])
{
#if MLP_N_LAYERS > 0
    static float a[MLP_MAX_WIDTH], b[MLP_MAX_WIDTH];
    for (int i = 0; i < FEAT_N; i++) {
        a[i] = (feat[i] - model_feat_mean[i]) / model_feat_std[i];
    }
    for (int l = 0; l < MLP_N_LAYERS; l++) {
        const int nin = mlp_dims[l], nout = mlp_dims[l + 1];
        const float *W = mlp_w[l];
        for (int o = 0; o < nout; o++) {
            float s = mlp_b[l][o];
            const float *row = &W[o * nin];
            for (int i = 0; i < nin; i++) {
                s += row[i] * a[i];
            }
            b[o] = (l < MLP_N_LAYERS - 1 && s < 0.0f) ? 0.0f : s;
        }
        for (int o = 0; o < nout; o++) {
            a[o] = b[o];
        }
    }
    float mx = a[0];
    for (int c = 1; c < MODEL_N_CLASSES; c++) {
        if (a[c] > mx) {
            mx = a[c];
        }
    }
    float sum = 0.0f;
    for (int c = 0; c < MODEL_N_CLASSES; c++) {
        prob[c] = expf(a[c] - mx);
        sum += prob[c];
    }
    int best = 0;
    for (int c = 0; c < MODEL_N_CLASSES; c++) {
        prob[c] /= sum;
        if (prob[c] > prob[best]) {
            best = c;
        }
    }
    return best;
#else
    (void)feat;
    for (int c = 0; c < MODEL_N_CLASSES; c++) {
        prob[c] = 0.0f;
    }
    return -1;
#endif
}
