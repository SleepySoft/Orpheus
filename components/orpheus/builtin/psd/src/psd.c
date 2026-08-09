#include "orpheus_psd.h"

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

static const OrpheusParameter psd_params[] = {
    { .id = "channels", .name = "通道数", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "smoothing", .name = "平滑块数", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 8 },
      .min_i32 = 1, .max_i32 = 128, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "spectrum", .name = "功率谱", .type = ORPHEUS_VALUE_STRING,
      .default_value = { .type = ORPHEUS_VALUE_STRING, .value.str = "[]" },
      .update_policy = ORPHEUS_UPDATE_IMMEDIATE,
      .readback = true, .persistent = false, .affects_signature = false }
};

static const OrpheusPort psd_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor psd_descriptor = {
    .id = "orpheus.builtin.psd", .version = "1.0.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = psd_ports, .port_count = 2, .params = psd_params, .param_count = 3,
    .state_size = sizeof(PsdState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = true
};

static const OrpheusComponentDescriptor* psd_get_descriptor(void) { return &psd_descriptor; }

static int psd_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(PsdState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}

static void psd_free(PsdState* s) {
    free(s->mags); free(s->re); free(s->im);
    s->mags = s->re = s->im = NULL;
}
static int psd_destroy(void* state) { psd_free((PsdState*)state); return ORPHEUS_OK; }

static int psd_prepare(void* state, const OrpheusConfig* config) {
    PsdState* s = (PsdState*)state;
    psd_free(s);
    s->channels = config->channels > 0 ? config->channels : 2;
    s->half = config->block_size / 2;
    if (s->half < 2 || s->half > PSD_MAX_HALF) s->half = PSD_MAX_HALF;
    s->smoothing = 1.0f / 8.0f;
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], "smoothing") == 0 &&
            config->param_values[i].type == ORPHEUS_VALUE_INT) {
            int sm = config->param_values[i].value.i32;
            s->smoothing = 1.0f / (float)(sm > 0 ? sm : 8);
        }
    }
    s->mags = (float*)calloc(s->channels * s->half, sizeof(float));
    s->re = (float*)malloc(s->half * 2 * sizeof(float));
    s->im = (float*)malloc(s->half * 2 * sizeof(float));
    if (!s->mags || !s->re || !s->im) { psd_free(s); return ORPHEUS_ERR_OUT_OF_MEMORY; }
    s->json[0] = '\0';
    return ORPHEUS_OK;
}

static int psd_reset(void* state) {
    PsdState* s = (PsdState*)state;
    if (s->mags) memset(s->mags, 0, s->channels * s->half * sizeof(float));
    s->json[0] = '\0';
    return ORPHEUS_OK;
}

static int psd_process(void* state, const OrpheusProcessContext* ctx) {
    PsdState* s = (PsdState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (in == NULL || out == NULL) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;
    if (out != in) memcpy(out_data, in_data, frames * ch * sizeof(float));
    out->frame_count = frames;
    if (!is_pow2(frames) || frames / 2 != s->half) return ORPHEUS_OK;

    uint32_t n = frames;
    uint32_t half = s->half;
    float a = s->smoothing;
    for (uint32_t c = 0; c < ch; ++c) {
        for (uint32_t f = 0; f < n; ++f) {
            s->re[f] = in_data[f * ch + c];
            s->im[f] = 0.0f;
        }
        fft_radix2(s->re, s->im, n);
        float* mag = s->mags + c * half;
        for (uint32_t k = 0; k < half; ++k) {
            float m = 2.0f * sqrtf(s->re[k] * s->re[k] + s->im[k] * s->im[k]) / (float)n;
            mag[k] += a * (m - mag[k]);
        }
    }
    /* readback：通道 0 的平滑幅度（与 probe_spectrum 同构，复用频谱 widget） */
    float* mag0 = s->mags;
    char* p = s->json;
    size_t rem = sizeof(s->json);
    int len = snprintf(p, rem, "[");
    p += len; rem -= (size_t)len;
    for (uint32_t k = 0; k < half && rem > 8; ++k) {
        len = snprintf(p, rem, k ? ",%.6g" : "%.6g", mag0[k]);
        p += len; rem -= (size_t)len;
    }
    if (rem > 2) snprintf(p, rem, "]");
    return ORPHEUS_OK;
}

static int psd_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    PsdState* s = (PsdState*)state;
    if (strcmp(param_id, "spectrum") == 0) {
        value->type = ORPHEUS_VALUE_STRING;
        value->value.str = s->json;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)s->channels;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int psd_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    (void)state; (void)param_id; (void)value;
    return ORPHEUS_ERR_NOT_FOUND;
}

static int psd_register_slots(void* state, const OrpheusRegistry* reg) {
    PsdState* s = (PsdState*)state;
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, smoothing, ORPHEUS_SLOT_SETTING, "smoothing", "平滑块数",
                     ORPHEUS_VALUE_FLOAT, .min_f32=1.0f, .max_f32=128.0f,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, json, ORPHEUS_SLOT_PROBE, "spectrum", "功率谱",
                     ORPHEUS_VALUE_STRING, .flags=ORPHEUS_SLOT_READBACK);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface psd_interface = {
    .get_descriptor = psd_get_descriptor, .create = psd_create, .destroy = psd_destroy,
    .prepare = psd_prepare, .reset = psd_reset, .process = psd_process,
    .set_parameter = psd_set_parameter, .get_parameter = psd_get_parameter,
    .get_state_value = NULL, .register_slots = psd_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {
    return &psd_interface;
}
