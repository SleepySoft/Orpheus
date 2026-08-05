#include "orpheus_mixer.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    float gain_linear[2];
    float target_gain_linear[2];
    float smoothing_coeff;
    uint32_t channels;
} MixerState;

static const OrpheusParameter mixer_params[] = {
    {
        .id = "gain0",
        .name = "Gain 0",
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
        .id = "gain1",
        .name = "Gain 1",
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
    }
};

static const OrpheusPort mixer_ports[] = {
    {
        .id = "in0",
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
        .id = "in1",
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

static const OrpheusComponentDescriptor mixer_descriptor = {
    .id = "orpheus.builtin.mixer",
    .version = "1.0.0",
    .abi_version = ORPHEUS_ABI_VERSION,
    .ports = mixer_ports,
    .port_count = 3,
    .params = mixer_params,
    .param_count = 3,
    .state_size = sizeof(MixerState),
    .scratch_size = 0,
    .alignment = 8,
    .latency_samples = 0,
    .realtime_safe = true,
    .supports_inplace = false
};

static float db_to_linear(float db) {
    return powf(10.0f, db / 20.0f);
}

static const OrpheusComponentDescriptor* mixer_get_descriptor(void) {
    return &mixer_descriptor;
}

static int mixer_create(void** state, const OrpheusConfig* config) {
    (void)config;
    *state = calloc(1, sizeof(MixerState));
    if (*state == NULL) return ORPHEUS_ERR_OUT_OF_MEMORY;
    return ORPHEUS_OK;
}

static int mixer_destroy(void* state) {
    free(state);
    return ORPHEUS_OK;
}

static int mixer_prepare(void* state, const OrpheusConfig* config) {
    MixerState* s = (MixerState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    s->gain_linear[0] = db_to_linear(0.0f);
    s->gain_linear[1] = db_to_linear(0.0f);
    s->target_gain_linear[0] = s->gain_linear[0];
    s->target_gain_linear[1] = s->gain_linear[1];
    s->smoothing_coeff = 1.0f;
    return ORPHEUS_OK;
}

static int mixer_reset(void* state) {
    MixerState* s = (MixerState*)state;
    s->gain_linear[0] = db_to_linear(0.0f);
    s->gain_linear[1] = db_to_linear(0.0f);
    s->target_gain_linear[0] = s->gain_linear[0];
    s->target_gain_linear[1] = s->gain_linear[1];
    return ORPHEUS_OK;
}

static int mixer_process(void* state, const OrpheusProcessContext* ctx) {
    MixerState* s = (MixerState*)state;
    const OrpheusBuffer* in0 = ctx->inputs[0];
    const OrpheusBuffer* in1 = ctx->inputs[1];
    OrpheusBuffer* out = ctx->outputs[0];

    if (in0 == NULL || in1 == NULL || out == NULL) return ORPHEUS_ERR_INVALID_ARG;

    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    const float* data0 = (const float*)in0->data;
    const float* data1 = (const float*)in1->data;
    float* out_data = (float*)out->data;

    for (uint32_t i = 0; i < frames * ch; ++i) {
        s->gain_linear[0] += s->smoothing_coeff * (s->target_gain_linear[0] - s->gain_linear[0]);
        s->gain_linear[1] += s->smoothing_coeff * (s->target_gain_linear[1] - s->gain_linear[1]);
        out_data[i] = data0[i] * s->gain_linear[0] + data1[i] * s->gain_linear[1];
    }

    out->frame_count = frames;
    return ORPHEUS_OK;
}

static int mixer_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    MixerState* s = (MixerState*)state;
    if (strcmp(param_id, "gain0") == 0) {
        if (value->type != ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        s->target_gain_linear[0] = db_to_linear(value->value.f32);
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "gain1") == 0) {
        if (value->type != ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        s->target_gain_linear[1] = db_to_linear(value->value.f32);
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int mixer_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    MixerState* s = (MixerState*)state;
    if (strcmp(param_id, "gain0") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT;
        value->value.f32 = 20.0f * log10f(s->gain_linear[0]);
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "gain1") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT;
        value->value.f32 = 20.0f * log10f(s->gain_linear[1]);
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)s->channels;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static const OrpheusComponentInterface mixer_interface = {
    .get_descriptor = mixer_get_descriptor,
    .create = mixer_create,
    .destroy = mixer_destroy,
    .prepare = mixer_prepare,
    .reset = mixer_reset,
    .process = mixer_process,
    .set_parameter = mixer_set_parameter,
    .get_parameter = mixer_get_parameter,
    .get_state_value = NULL
};

ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void) {
    return &mixer_interface;
}
