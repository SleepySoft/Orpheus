#include "orpheus_fir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const OrpheusParameter fir_params[] = {
    {
        .id = "coefficients",
        .name = "Coefficients",
        .type = ORPHEUS_VALUE_STRING,
        .default_value = { .type = ORPHEUS_VALUE_STRING, .value.str = "1.0" },
        .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
        .persistent = true,
        .affects_signature = false
    },
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
        .id = "taps",
        .name = "Taps",
        .type = ORPHEUS_VALUE_INT,
        .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 0 },
        .update_policy = ORPHEUS_UPDATE_IMMEDIATE,
        .readback = true,
        .persistent = false,
        .affects_signature = false
    }
};

static const OrpheusPort fir_ports[] = {
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

static const OrpheusComponentDescriptor fir_descriptor = {
    .id = "orpheus.builtin.fir",
    .version = "1.0.0",
    .abi_version = ORPHEUS_ABI_VERSION,
    .ports = fir_ports,
    .port_count = 2,
    .params = fir_params,
    .param_count = 3,
    .state_size = sizeof(FirState),
    .scratch_size = 0,
    .alignment = 8,
    .latency_samples = 0,
    .realtime_safe = true,
    .supports_inplace = true
};

static const OrpheusComponentDescriptor* fir_get_descriptor(void) {
    return &fir_descriptor;
}

static int fir_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(FirState));
    if (*state == NULL) return ORPHEUS_ERR_OUT_OF_MEMORY;
    return ORPHEUS_OK;
}

static void fir_free_state(FirState* s) {
    free(s->coeffs);
    free(s->delay);
    free(s->pos);
    s->coeffs = NULL;
    s->delay = NULL;
    s->pos = NULL;
    s->taps = 0;
}

static int fir_destroy(void* state) {
    FirState* s = (FirState*)state;
    fir_free_state(s);
    /* v2：状态块本身由 Runtime 统一管理 */
    return ORPHEUS_OK;
}

/* 解析逗号/空白分隔的浮点系数列表；空则退化为 1 阶直通。 */
static uint32_t fir_parse_coefficients(const char* text, float* out, uint32_t max_taps) {
    uint32_t n = 0;
    if (!text) return 0;
    const char* p = text;
    while (*p && n < max_taps) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (!*p) break;
        char* end = NULL;
        float v = (float)strtod(p, &end);
        if (end == p) break;
        out[n++] = v;
        p = end;
    }
    return n;
}

static int fir_prepare(void* state, const OrpheusConfig* config) {
    FirState* s = (FirState*)state;
    fir_free_state(s);
    s->channels = config->channels > 0 ? config->channels : 2;

    float tmp[FIR_MAX_TAPS];
    uint32_t taps = 0;
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], "coefficients") == 0 &&
            config->param_values[i].type == ORPHEUS_VALUE_STRING) {
            taps = fir_parse_coefficients(config->param_values[i].value.str, tmp, FIR_MAX_TAPS);
        }
    }
    if (taps == 0) {
        tmp[0] = 1.0f;
        taps = 1;
    }

    s->coeffs = (float*)malloc(taps * sizeof(float));
    s->delay = (float*)calloc((size_t)taps * s->channels, sizeof(float));
    s->pos = (uint32_t*)calloc(s->channels, sizeof(uint32_t));
    if (!s->coeffs || !s->delay || !s->pos) {
        fir_free_state(s);
        return ORPHEUS_ERR_OUT_OF_MEMORY;
    }
    memcpy(s->coeffs, tmp, taps * sizeof(float));
    s->taps = taps;
    return ORPHEUS_OK;
}

static int fir_reset(void* state) {
    FirState* s = (FirState*)state;
    if (s->delay) memset(s->delay, 0, (size_t)s->taps * s->channels * sizeof(float));
    if (s->pos) memset(s->pos, 0, s->channels * sizeof(uint32_t));
    return ORPHEUS_OK;
}

static int fir_process(void* state, const OrpheusProcessContext* ctx) {
    FirState* s = (FirState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (!in || !out) return ORPHEUS_ERR_INVALID_ARG;

    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;
    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    uint32_t T = s->taps;
    if (T == 0) T = 1;

    for (uint32_t f = 0; f < frames; ++f) {
        for (uint32_t c = 0; c < ch; ++c) {
            float x = in_data[f * ch + c];
            uint32_t p = s->pos[c];
            s->delay[p * ch + c] = x; /* 先写当前采样进延迟线 */
            float acc = 0.0f;
            for (uint32_t k = 0; k < T; ++k) {
                uint32_t idx = (p + T - k) % T; /* k=0 为最新采样 */
                acc += s->coeffs[k] * s->delay[idx * ch + c];
            }
            out_data[f * ch + c] = acc;
            s->pos[c] = (p + 1) % T;
        }
    }
    out->frame_count = frames;
    out->interleaved = true;
    return ORPHEUS_OK;
}

static int fir_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    (void)state; (void)param_id; (void)value;
    return ORPHEUS_ERR_UNSUPPORTED;
}

static int fir_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    FirState* s = (FirState*)state;
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)s->channels;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "taps") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)s->taps;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int fir_register_slots(void* state, const OrpheusRegistry* reg) {
    FirState* s = (FirState*)state;
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, taps, ORPHEUS_SLOT_PROBE, "taps", "抽头数",
                     ORPHEUS_VALUE_INT, .flags=ORPHEUS_SLOT_READBACK);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface fir_interface = {
    .get_descriptor = fir_get_descriptor,
    .create = fir_create,
    .destroy = fir_destroy,
    .prepare = fir_prepare,
    .reset = fir_reset,
    .process = fir_process,
    .set_parameter = fir_set_parameter,
    .get_parameter = fir_get_parameter,
    .get_state_value = NULL,
    .register_slots = fir_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {
    return &fir_interface;
}
