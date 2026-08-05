#include "orpheus_gain.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* 状态：当前线性增益与目标线性增益 */
typedef struct {
    float gain_linear;
    float target_linear;
    float smoothing_coeff;
    uint32_t channels;
} GainState;

static float db_to_linear(float db) {
    return powf(10.0f, db / 20.0f);
}

static const OrpheusParameter gain_params[] = {
    {
        .id = "gain_db",
        .name = "Gain",
        .type = ORPHEUS_VALUE_FLOAT,
        .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.0f },
        .min_f32 = -96.0f,
        .max_f32 = 24.0f,
        .unit = "dB",
        .update_policy = ORPHEUS_UPDATE_SMOOTHED,
        .readback = true,
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
        .id = "smoothing_ms",
        .name = "Smoothing Time",
        .type = ORPHEUS_VALUE_FLOAT,
        .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 5.0f },
        .min_f32 = 0.0f,
        .max_f32 = 1000.0f,
        .unit = "ms",
        .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
        .readback = true,
        .persistent = true,
        .affects_signature = false
    }
};

static const OrpheusPort gain_ports[] = {
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

static const OrpheusComponentDescriptor gain_descriptor = {
    .id = "orpheus.builtin.gain",
    .version = "1.0.0",
    .abi_version = ORPHEUS_ABI_VERSION,
    .ports = gain_ports,
    .port_count = 2,
    .params = gain_params,
    .param_count = 3,
    .state_size = sizeof(GainState),
    .scratch_size = 0,
    .alignment = 8,
    .latency_samples = 0,
    .realtime_safe = true,
    .supports_inplace = true
};

static const OrpheusComponentDescriptor* gain_get_descriptor_impl(void) {
    return &gain_descriptor;
}

static int gain_create(void** state, const OrpheusConfig* config) {
    (void)config;
    *state = calloc(1, sizeof(GainState));
    if (*state == NULL) {
        return ORPHEUS_ERR_OUT_OF_MEMORY;
    }
    return ORPHEUS_OK;
}

static int gain_destroy(void* state) {
    free(state);
    return ORPHEUS_OK;
}

static int gain_prepare(void* state, const OrpheusConfig* config) {
    GainState* s = (GainState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;

    /* 初始参数：gain_db 与 smoothing_ms 都从 prepare 配置读取 */
    float gain_db = 0.0f;
    float smoothing_ms = 5.0f;
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] == NULL) continue;
        if (strcmp(config->param_ids[i], "gain_db") == 0 &&
            config->param_values[i].type == ORPHEUS_VALUE_FLOAT) {
            gain_db = config->param_values[i].value.f32;
        } else if (strcmp(config->param_ids[i], "smoothing_ms") == 0 &&
                   config->param_values[i].type == ORPHEUS_VALUE_FLOAT) {
            smoothing_ms = config->param_values[i].value.f32;
        }
    }
    s->gain_linear = db_to_linear(gain_db);
    s->target_linear = s->gain_linear;

    if (smoothing_ms <= 0.0f || config->sample_rate == 0) {
        s->smoothing_coeff = 1.0f;
    } else {
        float tau = smoothing_ms / 1000.0f;
        s->smoothing_coeff = 1.0f - expf(-1.0f / (tau * (float)config->sample_rate));
        if (s->smoothing_coeff > 1.0f) s->smoothing_coeff = 1.0f;
    }

    return ORPHEUS_OK;
}

static int gain_reset(void* state) {
    GainState* s = (GainState*)state;
    s->gain_linear = db_to_linear(0.0f);
    s->target_linear = s->gain_linear;
    return ORPHEUS_OK;
}

static int gain_process(void* state, const OrpheusProcessContext* ctx) {
    GainState* s = (GainState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];

    if (in == NULL || out == NULL) {
        return ORPHEUS_ERR_INVALID_ARG;
    }

    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    float* in_data = (float*)in->data;
    float* out_data = (float*)out->data;

    /* 平滑过渡增益 */
    for (uint32_t i = 0; i < frames * ch; ++i) {
        s->gain_linear += s->smoothing_coeff * (s->target_linear - s->gain_linear);
        out_data[i] = in_data[i] * s->gain_linear;
    }

    out->frame_count = frames;
    return ORPHEUS_OK;
}

static int gain_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    GainState* s = (GainState*)state;
    if (strcmp(param_id, "gain_db") == 0) {
        if (value->type != ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        s->target_linear = db_to_linear(value->value.f32);
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int gain_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    GainState* s = (GainState*)state;
    if (strcmp(param_id, "gain_db") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT;
        value->value.f32 = 20.0f * log10f(s->gain_linear);
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)s->channels;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static const OrpheusComponentInterface gain_interface = {
    .get_descriptor = gain_get_descriptor_impl,
    .create = gain_create,
    .destroy = gain_destroy,
    .prepare = gain_prepare,
    .reset = gain_reset,
    .process = gain_process,
    .set_parameter = gain_set_parameter,
    .get_parameter = gain_get_parameter,
    .get_state_value = NULL
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {
    return &gain_interface;
}
