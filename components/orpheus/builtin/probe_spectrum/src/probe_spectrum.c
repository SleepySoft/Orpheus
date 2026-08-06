#include "orpheus_probe_spectrum.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const OrpheusParameter params[] = {
    {
        .id = "channels",
        .name = "Channels",
        .type = ORPHEUS_VALUE_INT,
        .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
        .min_i32 = 1,
        .max_i32 = 32,
        .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
        .readback = true,
        .persistent = true,
        .affects_signature = true
    },
    {
        .id = "window_size",
        .name = "FFT Window",
        .type = ORPHEUS_VALUE_INT,
        .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 1024 },
        .min_i32 = 64,
        .max_i32 = 4096,
        .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
        .persistent = true,
        .affects_signature = false
    },
    {
        .id = "spectrum",
        .name = "Spectrum",
        .type = ORPHEUS_VALUE_STRING,
        .default_value = { .type = ORPHEUS_VALUE_STRING, .value.str = "" },
        .update_policy = ORPHEUS_UPDATE_IMMEDIATE,
        .readback = true,
        .persistent = false,
        .affects_signature = false
    }
};

static const OrpheusPort ports[] = {
    {
        .id = "in",
        .direction = ORPHEUS_PORT_INPUT,
        .type = ORPHEUS_PORT_AUDIO,
        .sample_format = ORPHEUS_FORMAT_F32,
        .channels = 0,
        .sample_rate = 0,
        .block_size = 0,
        .is_variable = true,
        .channels_param = "channels"
    },
    {
        .id = "out",
        .direction = ORPHEUS_PORT_OUTPUT,
        .type = ORPHEUS_PORT_AUDIO,
        .sample_format = ORPHEUS_FORMAT_F32,
        .channels = 0,
        .sample_rate = 0,
        .block_size = 0,
        .is_variable = true,
        .channels_param = "channels"
    }
};

static const OrpheusComponentDescriptor desc = {
    .id = "orpheus.builtin.probe_spectrum",
    .version = "1.0.0",
    .abi_version = ORPHEUS_ABI_VERSION,
    .ports = ports,
    .port_count = 2,
    .params = params,
    .param_count = 3,
    .state_size = sizeof(SpectrumState),
    .scratch_size = 0,
    .alignment = 8,
    .latency_samples = 0,
    .realtime_safe = true,
    .supports_inplace = true
};

static const OrpheusComponentDescriptor* get_desc(void) {
    return &desc;
}

static int create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(SpectrumState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}

static void free_state(SpectrumState* s) {
    free(s->window);
    free(s->ring);
    free(s->re);
    free(s->im);
    free(s->mags);
    s->window = s->ring = s->re = s->im = s->mags = NULL;
}

static int destroy(void* state) {
    SpectrumState* s = (SpectrumState*)state;
    free_state(s);
    /* v2：状态块本身由 Runtime 统一管理 */
    return ORPHEUS_OK;
}

/* 迭代 radix-2 FFT，就地运算，n 必须为 2 的幂 */
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

static uint32_t next_pow2(uint32_t v) {
    uint32_t p = 64;
    while (p < v && p < SPECTRUM_MAX_WINDOW) p <<= 1;
    return p;
}

static int prepare(void* state, const OrpheusConfig* config) {
    SpectrumState* s = (SpectrumState*)state;
    free_state(s);
    s->channels = config->channels > 0 ? config->channels : 2;
    s->window_size = 1024;
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], "window_size") == 0 &&
            config->param_values[i].type == ORPHEUS_VALUE_INT) {
            s->window_size = (uint32_t)config->param_values[i].value.i32;
        }
    }
    s->window_size = next_pow2(s->window_size);
    s->half = s->window_size / 2;
    s->ring_pos = 0;
    s->samples_seen = 0;

    s->window = (float*)malloc(s->window_size * sizeof(float));
    s->ring = (float*)calloc(s->window_size, sizeof(float));
    s->re = (float*)malloc(s->window_size * sizeof(float));
    s->im = (float*)malloc(s->window_size * sizeof(float));
    s->mags = (float*)calloc(s->half, sizeof(float));
    if (!s->window || !s->ring || !s->re || !s->im || !s->mags) {
        free_state(s);
        return ORPHEUS_ERR_OUT_OF_MEMORY;
    }
    for (uint32_t i = 0; i < s->window_size; ++i) {
        s->window[i] = 0.5f - 0.5f * cosf(2.0f * 3.14159265358979323846f * i / (s->window_size - 1));
    }
    s->json[0] = '\0';
    return ORPHEUS_OK;
}

static int reset(void* state) {
    SpectrumState* s = (SpectrumState*)state;
    s->ring_pos = 0;
    s->samples_seen = 0;
    if (s->ring) memset(s->ring, 0, s->window_size * sizeof(float));
    if (s->mags) memset(s->mags, 0, s->half * sizeof(float));
    return ORPHEUS_OK;
}

static void analyze(SpectrumState* s) {
    uint32_t n = s->window_size;
    for (uint32_t i = 0; i < n; ++i) {
        float x = s->ring[i] * s->window[i];
        s->re[i] = x;
        s->im[i] = 0.0f;
    }
    fft_radix2(s->re, s->im, n);
    for (uint32_t i = 0; i < s->half; ++i) {
        s->mags[i] = 2.0f * sqrtf(s->re[i] * s->re[i] + s->im[i] * s->im[i]) / (float)n;
    }
}

static int process(void* state, const OrpheusProcessContext* ctx) {
    SpectrumState* s = (SpectrumState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (!in) return ORPHEUS_ERR_INVALID_ARG;

    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels > 0 ? s->channels : 1;
    const float* in_data = (const float*)in->data;
    for (uint32_t f = 0; f < frames; ++f) {
        s->ring[s->ring_pos] = in_data[f * ch];
        s->ring_pos = (s->ring_pos + 1) % s->window_size;
        if (++s->samples_seen >= s->window_size) {
            s->samples_seen = 0;
            analyze(s);
        }
    }
    if (out) {
        float* out_data = (float*)out->data;
        if (out_data) memcpy(out_data, in_data, frames * ch * sizeof(float));
        out->frame_count = frames;
        out->interleaved = true;
    }
    return ORPHEUS_OK;
}

static int set_param(void* state, const char* id, const OrpheusValue* v) {
    (void)state; (void)id; (void)v;
    return ORPHEUS_ERR_UNSUPPORTED;
}

static int get_param(void* state, const char* id, OrpheusValue* v) {
    SpectrumState* s = (SpectrumState*)state;
    if (strcmp(id, "channels") == 0) {
        v->type = ORPHEUS_VALUE_INT;
        v->value.i32 = (int32_t)s->channels;
        return ORPHEUS_OK;
    }
    if (strcmp(id, "window_size") == 0) {
        v->type = ORPHEUS_VALUE_INT;
        v->value.i32 = (int32_t)s->window_size;
        return ORPHEUS_OK;
    }
    if (strcmp(id, "spectrum") == 0) {
        char* p = s->json;
        size_t rem = sizeof(s->json);
        int n = snprintf(p, rem, "[");
        if (n < 0) return ORPHEUS_ERR_PROCESSING;
        p += n; rem -= (size_t)n;
        for (uint32_t i = 0; i < s->half && rem > 16; ++i) {
            n = snprintf(p, rem, i ? ",%.6g" : "%.6g", s->mags[i]);
            if (n < 0 || (size_t)n >= rem) break;
            p += n; rem -= (size_t)n;
        }
        if (rem > 2) snprintf(p, rem, "]");
        v->type = ORPHEUS_VALUE_STRING;
        v->value.str = s->json;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int register_slots(void* state, const OrpheusRegistry* reg) {
    SpectrumState* s = (SpectrumState*)state;
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, window_size, ORPHEUS_SLOT_SETTING, "window_size", "FFT 窗口",
                     ORPHEUS_VALUE_INT, .min_i32=64, .max_i32=4096,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT);
    ORPHEUS_REG_SLOT(reg, s, json, ORPHEUS_SLOT_PROBE, "spectrum", "频谱",
                     ORPHEUS_VALUE_STRING, .flags=ORPHEUS_SLOT_READBACK);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface iface = {
    .get_descriptor = get_desc,
    .create = create,
    .destroy = destroy,
    .prepare = prepare,
    .reset = reset,
    .process = process,
    .set_parameter = set_param,
    .get_parameter = get_param,
    .get_state_value = NULL,
    .register_slots = register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {
    return &iface;
}
