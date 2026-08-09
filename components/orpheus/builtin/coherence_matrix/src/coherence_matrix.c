#include "orpheus_coherence_matrix.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t is_pow2(uint32_t v) { return v && (v & (v - 1)) == 0; }

static void fft_radix2(float* re, float* im, uint32_t n) {
    for (uint32_t i = 1, j = 0; i < n; ++i) {
        uint32_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            float t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (uint32_t len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * 3.14159265358979323846f / (float)len;
        float wr = cosf(ang), wi = sinf(ang);
        for (uint32_t i = 0; i < n; i += len) {
            float cwr = 1.0f, cwi = 0.0f;
            for (uint32_t k = 0; k < len / 2; ++k) {
                uint32_t a = i + k, b = i + k + len / 2;
                float tr = cwr * re[b] - cwi * im[b];
                float ti = cwr * im[b] + cwi * re[b];
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
                float nwr = cwr * wr - cwi * wi;
                cwi = cwr * wi + cwi * wr;
                cwr = nwr;
            }
        }
    }
}

static const OrpheusParameter cm_params[] = {
    { .id = "channels", .name = "通道数", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 2, .max_i32 = 10, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "smoothing", .name = "平滑块数", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 8 },
      .min_i32 = 1, .max_i32 = 128, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "coherence", .name = "相干矩阵", .type = ORPHEUS_VALUE_STRING,
      .default_value = { .type = ORPHEUS_VALUE_STRING, .value.str = "{}" },
      .update_policy = ORPHEUS_UPDATE_IMMEDIATE,
      .readback = true, .persistent = false, .affects_signature = false },
    { .id = "history", .name = "相干历史", .type = ORPHEUS_VALUE_STRING,
      .default_value = { .type = ORPHEUS_VALUE_STRING, .value.str = "[]" },
      .update_policy = ORPHEUS_UPDATE_IMMEDIATE,
      .readback = true, .persistent = false, .affects_signature = false }
};

static const OrpheusPort cm_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor cm_descriptor = {
    .id = "orpheus.builtin.coherence_matrix", .version = "1.0.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = cm_ports, .port_count = 2, .params = cm_params, .param_count = 4,
    .state_size = sizeof(CoherenceMatrixState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = true
};

static const OrpheusComponentDescriptor* cm_get_descriptor(void) { return &cm_descriptor; }

static int cm_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(CoherenceMatrixState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}

static void cm_free(CoherenceMatrixState* s) {
    free(s->gxx); free(s->gxy_re); free(s->gxy_im); free(s->rea); free(s->ima);
    s->gxx = s->gxy_re = s->gxy_im = s->rea = s->ima = NULL;
}
static int cm_destroy(void* state) { cm_free((CoherenceMatrixState*)state); return ORPHEUS_OK; }

static int cm_prepare(void* state, const OrpheusConfig* config) {
    CoherenceMatrixState* s = (CoherenceMatrixState*)state;
    cm_free(s);
    s->channels = config->channels > 0 ? config->channels : 2;
    if (s->channels < 2 || s->channels > CM_MAX_CHANNELS) s->channels = 2;
    s->half = config->block_size / 2;
    if (s->half < 2 || s->half > CM_MAX_HALF) s->half = CM_MAX_HALF;
    s->alpha = 1.0f / 8.0f;
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], "smoothing") == 0 &&
            config->param_values[i].type == ORPHEUS_VALUE_INT) {
            int sm = config->param_values[i].value.i32;
            s->alpha = 1.0f / (float)(sm > 0 ? sm : 8);
        }
    }
    uint32_t n = s->channels, h = s->half;
    s->gxx = (float*)calloc(n * h, sizeof(float));
    s->gxy_re = (float*)calloc(n * n * h, sizeof(float));
    s->gxy_im = (float*)calloc(n * n * h, sizeof(float));
    s->rea = (float*)malloc(n * h * 2 * sizeof(float)); /* 每通道 frames = 2×half */
    s->ima = (float*)malloc(n * h * 2 * sizeof(float));
    if (!s->gxx || !s->gxy_re || !s->gxy_im || !s->rea || !s->ima) {
        cm_free(s);
        return ORPHEUS_ERR_OUT_OF_MEMORY;
    }
    s->json_matrix[0] = s->json_history[0] = '\0';
    return ORPHEUS_OK;
}

static int cm_reset(void* state) {
    CoherenceMatrixState* s = (CoherenceMatrixState*)state;
    if (s->gxx) memset(s->gxx, 0, s->channels * s->half * sizeof(float));
    if (s->gxy_re) memset(s->gxy_re, 0, s->channels * s->channels * s->half * sizeof(float));
    if (s->gxy_im) memset(s->gxy_im, 0, s->channels * s->channels * s->half * sizeof(float));
    s->hist_pos = s->hist_count = 0;
    s->json_matrix[0] = s->json_history[0] = '\0';
    return ORPHEUS_OK;
}

static int cm_process(void* state, const OrpheusProcessContext* ctx) {
    CoherenceMatrixState* s = (CoherenceMatrixState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (in == NULL || out == NULL) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t frames = ctx->frame_count;
    uint32_t n = s->channels, h = s->half;
    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;
    if (out != in) memcpy(out_data, in_data, frames * n * sizeof(float));
    out->frame_count = frames;
    if (!is_pow2(frames) || frames / 2 != h) return ORPHEUS_OK;

    float a = s->alpha;
    /* 1) 每通道 FFT */
    for (uint32_t c = 0; c < n; ++c) {
        float* re = s->rea + c * h;
        float* im = s->ima + c * h;
        for (uint32_t f = 0; f < frames; ++f) re[f] = in_data[f * n + c];
        memset(im, 0, frames * sizeof(float));
        fft_radix2(re, im, frames);
    }
    /* 2) 交叉功率谱 EMA + 通道间相干（平均谱） */
    for (uint32_t k = 0; k < h; ++k) {
        for (uint32_t i = 0; i < n; ++i) {
            float xi_re = s->rea[i * h + k], xi_im = s->ima[i * h + k];
            float pxx = xi_re * xi_re + xi_im * xi_im;
            s->gxx[i * h + k] += a * (pxx - s->gxx[i * h + k]);
            for (uint32_t j = 0; j < n; ++j) {
                float xj_re = s->rea[j * h + k], xj_im = s->ima[j * h + k];
                float re = xi_re * xj_re + xi_im * xj_im;   /* Xi·conj(Xj) */
                float im = xi_im * xj_re - xi_re * xj_im;
                size_t off = ((size_t)i * n + j) * h + k;
                s->gxy_re[off] += a * (re - s->gxy_re[off]);
                s->gxy_im[off] += a * (im - s->gxy_im[off]);
            }
        }
    }
    /* 3) N×N 平均相干矩阵 + 历史（对角除外均值） */
    float matrix[CM_MAX_CHANNELS * CM_MAX_CHANNELS];
    float mean_off = 0.0f;
    uint32_t off_cnt = 0;
    for (uint32_t i = 0; i < n; ++i) {
        for (uint32_t j = 0; j < n; ++j) {
            double acc = 0.0;
            for (uint32_t k = 0; k < h; ++k) {
                size_t off = ((size_t)i * n + j) * h + k;
                float gxx = s->gxx[i * h + k], gyy = s->gxx[j * h + k];
                float mag2 = s->gxy_re[off] * s->gxy_re[off] + s->gxy_im[off] * s->gxy_im[off];
                float denom = gxx * gyy;
                acc += denom > 1e-12f ? mag2 / denom : 0.0;
            }
            float c = (float)(acc / (double)h);
            if (c > 1.0f) c = 1.0f;
            matrix[i * n + j] = c;
            if (i != j) { mean_off += c; off_cnt++; }
        }
    }
    s->hist[s->hist_pos] = off_cnt ? mean_off / (float)off_cnt : 0.0f;
    s->hist_pos = (s->hist_pos + 1) % CM_HISTORY;
    if (s->hist_count < CM_HISTORY) s->hist_count++;

    /* 4) readback JSON */
    {
        char* p = s->json_matrix;
        size_t rem = sizeof(s->json_matrix);
        int len = snprintf(p, rem, "{\"n\":%u,\"bins\":%u,\"matrix\":[", n, h);
        p += len; rem -= (size_t)len;
        for (uint32_t i = 0; i < n * n && rem > 8; ++i) {
            len = snprintf(p, rem, i ? ",%.4g" : "%.4g", matrix[i]);
            p += len; rem -= (size_t)len;
        }
        if (rem > 4) snprintf(p, rem, "]}");
    }
    {
        char* p = s->json_history;
        size_t rem = sizeof(s->json_history);
        int len = snprintf(p, rem, "[");
        p += len; rem -= (size_t)len;
        uint32_t cnt = s->hist_count;
        for (uint32_t i = 0; i < cnt && rem > 8; ++i) {
            uint32_t idx = (s->hist_pos + CM_HISTORY - cnt + i) % CM_HISTORY;
            len = snprintf(p, rem, i ? ",%.4g" : "%.4g", s->hist[idx]);
            p += len; rem -= (size_t)len;
        }
        if (rem > 2) snprintf(p, rem, "]");
    }
    return ORPHEUS_OK;
}

static int cm_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    CoherenceMatrixState* s = (CoherenceMatrixState*)state;
    if (strcmp(param_id, "coherence") == 0) {
        value->type = ORPHEUS_VALUE_STRING;
        value->value.str = s->json_matrix;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "history") == 0) {
        value->type = ORPHEUS_VALUE_STRING;
        value->value.str = s->json_history;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)s->channels;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int cm_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    (void)state; (void)param_id; (void)value;
    return ORPHEUS_ERR_NOT_FOUND;
}

static int cm_register_slots(void* state, const OrpheusRegistry* reg) {
    CoherenceMatrixState* s = (CoherenceMatrixState*)state;
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=2, .max_i32=10,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, alpha, ORPHEUS_SLOT_SETTING, "smoothing", "平滑块数",
                     ORPHEUS_VALUE_FLOAT, .min_f32=1.0f, .max_f32=128.0f,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, json_matrix, ORPHEUS_SLOT_PROBE, "coherence", "相干矩阵",
                     ORPHEUS_VALUE_STRING, .flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, json_history, ORPHEUS_SLOT_PROBE, "history", "相干历史",
                     ORPHEUS_VALUE_STRING, .flags=ORPHEUS_SLOT_READBACK);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface cm_interface = {
    .get_descriptor = cm_get_descriptor, .create = cm_create, .destroy = cm_destroy,
    .prepare = cm_prepare, .reset = cm_reset, .process = cm_process,
    .set_parameter = cm_set_parameter, .get_parameter = cm_get_parameter,
    .get_state_value = NULL, .register_slots = cm_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {
    return &cm_interface;
}
