#include "orpheus_mute.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static float read_float(const OrpheusConfig* config, const char* id, float fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_FLOAT) return config->param_values[i].value.f32;
            if (config->param_values[i].type == ORPHEUS_VALUE_INT) return (float)config->param_values[i].value.i32;
        }
    }
    return fallback;
}

static const OrpheusParameter mute_params[] = {
    { .id = "mute", .name = "Mute", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.0f },
      .min_f32 = 0.0f, .max_f32 = 1.0f, .update_policy = ORPHEUS_UPDATE_SMOOTHED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "channels", .name = "Channels", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "ramp_ms", .name = "Ramp Time", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 20.0f },
      .min_f32 = 0.0f, .max_f32 = 1000.0f, .unit = "ms", .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = false }
};

static const OrpheusPort mute_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor mute_descriptor = {
    .id = "orpheus.builtin.mute", .version = "1.0.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = mute_ports, .port_count = 2, .params = mute_params, .param_count = 3,
    .state_size = sizeof(MuteState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = true
};

static const OrpheusComponentDescriptor* mute_get_descriptor(void) { return &mute_descriptor; }

static int mute_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(MuteState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int mute_destroy(void* state) { (void)state; return ORPHEUS_OK; } /* v2：内存由 Runtime 统一管理 */

static int mute_prepare(void* state, const OrpheusConfig* config) {
    MuteState* s = (MuteState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    s->mute = read_float(config, "mute", 0.0f);
    s->target_linear = (s->mute > 0.5f) ? 0.0f : 1.0f;
    s->gain_linear = s->target_linear;
    float ramp_ms = read_float(config, "ramp_ms", 20.0f);
    if (ramp_ms <= 0.0f || config->sample_rate == 0) {
        s->smoothing_coeff = 1.0f;
    } else {
        float tau = ramp_ms / 1000.0f;
        s->smoothing_coeff = 1.0f - expf(-1.0f / (tau * (float)config->sample_rate));
        if (s->smoothing_coeff > 1.0f) s->smoothing_coeff = 1.0f;
    }
    s->ramp_ms = ramp_ms;
    return ORPHEUS_OK;
}
static int mute_reset(void* state) {
    MuteState* s = (MuteState*)state;
    s->target_linear = (s->mute > 0.5f) ? 0.0f : 1.0f;
    s->gain_linear = s->target_linear;
    return ORPHEUS_OK;
}
static int mute_process(void* state, const OrpheusProcessContext* ctx) {
    MuteState* s = (MuteState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (in == NULL || out == NULL) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t n = ctx->frame_count * s->channels;
    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;
    for (uint32_t i = 0; i < n; ++i) {
        s->gain_linear += s->smoothing_coeff * (s->target_linear - s->gain_linear);
        out_data[i] = in_data[i] * s->gain_linear;
    }
    out->frame_count = ctx->frame_count;
    return ORPHEUS_OK;
}
static int mute_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    MuteState* s = (MuteState*)state;
    if (strcmp(param_id, "mute") == 0) {
        float m = 0.0f;
        if (value->type == ORPHEUS_VALUE_FLOAT) m = value->value.f32;
        else if (value->type == ORPHEUS_VALUE_INT) m = (float)value->value.i32;
        else if (value->type == ORPHEUS_VALUE_BOOL) m = value->value.b ? 1.0f : 0.0f;
        s->mute = m;
        s->target_linear = (m > 0.5f) ? 0.0f : 1.0f;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int mute_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    MuteState* s = (MuteState*)state;
    if (strcmp(param_id, "mute") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT; value->value.f32 = s->mute; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->channels; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int mute_register_slots(void* state, const OrpheusRegistry* reg) {
    MuteState* s = (MuteState*)state;
    ORPHEUS_REG_SLOT(reg, s, mute, ORPHEUS_SLOT_SETTING, "mute", "静音",
                     ORPHEUS_VALUE_FLOAT, .min_f32=0.0f, .max_f32=1.0f,
                     .update_policy=ORPHEUS_UPDATE_SMOOTHED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, ramp_ms, ORPHEUS_SLOT_SETTING, "ramp_ms", "斜坡时间",
                     ORPHEUS_VALUE_FLOAT, .min_f32=0.0f, .max_f32=1000.0f, .unit="ms",
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    return ORPHEUS_OK;
}
static const OrpheusComponentInterface mute_interface = {
    .get_descriptor = mute_get_descriptor, .create = mute_create, .destroy = mute_destroy,
    .prepare = mute_prepare, .reset = mute_reset, .process = mute_process,
    .set_parameter = mute_set_parameter, .get_parameter = mute_get_parameter, .get_state_value = NULL,
    .register_slots = mute_register_slots
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &mute_interface; }
