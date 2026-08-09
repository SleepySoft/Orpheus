#include "orpheus_sine_mod.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TWO_PI 6.283185307179586f

static float read_float(const OrpheusConfig* config, const char* id, float fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_FLOAT) return config->param_values[i].value.f32;
            if (config->param_values[i].type == ORPHEUS_VALUE_INT) return (float)config->param_values[i].value.i32;
        }
    }
    return fallback;
}

static const OrpheusParameter sm_params[] = {
    { .id = "freq_hz", .name = "调制频率", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 1.0f },
      .min_f32 = 0.01f, .max_f32 = 20000.0f, .unit = "Hz",
      .update_policy = ORPHEUS_UPDATE_SMOOTHED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "depth", .name = "调制深度", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 1.0f },
      .min_f32 = 0.0f, .max_f32 = 1.0f, .update_policy = ORPHEUS_UPDATE_SMOOTHED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "channels", .name = "通道数", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true }
};

static const OrpheusPort sm_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor sm_descriptor = {
    .id = "orpheus.builtin.sine_mod", .version = "1.0.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = sm_ports, .port_count = 2, .params = sm_params, .param_count = 3,
    .state_size = sizeof(SineModState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = true
};

static const OrpheusComponentDescriptor* sm_get_descriptor(void) { return &sm_descriptor; }

static int sm_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(SineModState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int sm_destroy(void* state) { (void)state; return ORPHEUS_OK; }

static int sm_prepare(void* state, const OrpheusConfig* config) {
    SineModState* s = (SineModState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    s->freq_hz = read_float(config, "freq_hz", 1.0f);
    s->depth = read_float(config, "depth", 1.0f);
    s->sample_rate = config->sample_rate > 0 ? (float)config->sample_rate : 48000.0f;
    s->phase_inc = TWO_PI * s->freq_hz / s->sample_rate;
    s->phase = 0.0f;
    return ORPHEUS_OK;
}

static int sm_reset(void* state) {
    SineModState* s = (SineModState*)state;
    s->phase = 0.0f;
    s->freq_hz = 1.0f;
    s->depth = 1.0f;
    return ORPHEUS_OK;
}

static int sm_process(void* state, const OrpheusProcessContext* ctx) {
    SineModState* s = (SineModState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (in == NULL || out == NULL) return ORPHEUS_ERR_INVALID_ARG;

    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;
    float depth = s->depth < 0.0f ? 0.0f : (s->depth > 1.0f ? 1.0f : s->depth);
    for (uint32_t i = 0; i < frames * ch; ++i) {
        float mod = 1.0f + depth * sinf(s->phase);
        if (mod < 0.0f) mod = 0.0f;
        out_data[i] = in_data[i] * mod;
        s->phase += s->phase_inc;
        if (s->phase >= TWO_PI) s->phase -= TWO_PI;
    }
    out->frame_count = frames;
    return ORPHEUS_OK;
}

static int sm_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    SineModState* s = (SineModState*)state;
    if (strcmp(param_id, "freq_hz") == 0) {
        if (value->type != ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        s->freq_hz = value->value.f32;
        s->phase_inc = TWO_PI * s->freq_hz / (s->sample_rate > 0 ? s->sample_rate : 48000.0f);
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "depth") == 0) {
        if (value->type != ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        s->depth = value->value.f32;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int sm_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    SineModState* s = (SineModState*)state;
    if (strcmp(param_id, "freq_hz") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT;
        value->value.f32 = s->freq_hz;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "depth") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT;
        value->value.f32 = s->depth;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)s->channels;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int sm_register_slots(void* state, const OrpheusRegistry* reg) {
    SineModState* s = (SineModState*)state;
    ORPHEUS_REG_SLOT(reg, s, freq_hz, ORPHEUS_SLOT_SETTING, "freq_hz", "调制频率",
                     ORPHEUS_VALUE_FLOAT, .min_f32=0.01f, .max_f32=20000.0f, .unit="Hz",
                     .update_policy=ORPHEUS_UPDATE_SMOOTHED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_DIRECT_WRITE);
    ORPHEUS_REG_SLOT(reg, s, depth, ORPHEUS_SLOT_SETTING, "depth", "调制深度",
                     ORPHEUS_VALUE_FLOAT, .min_f32=0.0f, .max_f32=1.0f,
                     .update_policy=ORPHEUS_UPDATE_SMOOTHED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_DIRECT_WRITE);
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface sm_interface = {
    .get_descriptor = sm_get_descriptor, .create = sm_create, .destroy = sm_destroy,
    .prepare = sm_prepare, .reset = sm_reset, .process = sm_process,
    .set_parameter = sm_set_parameter, .get_parameter = sm_get_parameter,
    .get_state_value = NULL, .register_slots = sm_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {
    return &sm_interface;
}
