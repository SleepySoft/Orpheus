#include "orpheus_switch.h"

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

static const OrpheusParameter switch_params[] = {
    { .id = "enable", .name = "使能", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 1.0f },
      .min_f32 = 0.0f, .max_f32 = 1.0f, .update_policy = ORPHEUS_UPDATE_SMOOTHED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "channels", .name = "通道数", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "ramp_ms", .name = "斜坡时间", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 20.0f },
      .min_f32 = 0.0f, .max_f32 = 1000.0f, .unit = "ms",
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = false }
};

static const OrpheusPort switch_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor switch_descriptor = {
    .id = "orpheus.builtin.switch", .version = "1.0.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = switch_ports, .port_count = 2, .params = switch_params, .param_count = 3,
    .state_size = sizeof(SwitchState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = true
};

static const OrpheusComponentDescriptor* switch_get_descriptor(void) { return &switch_descriptor; }

static int switch_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(SwitchState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int switch_destroy(void* state) { (void)state; return ORPHEUS_OK; }

static int switch_prepare(void* state, const OrpheusConfig* config) {
    SwitchState* s = (SwitchState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    s->enable = read_float(config, "enable", 1.0f);
    s->ramp_ms = read_float(config, "ramp_ms", 20.0f);
    s->gain = s->enable >= 0.5f ? 1.0f : 0.0f;
    s->target = s->gain;
    if (s->ramp_ms <= 0.0f || config->sample_rate == 0) {
        s->smoothing_coeff = 1.0f;
    } else {
        s->smoothing_coeff = 1.0f - expf(-1.0f / ((s->ramp_ms / 1000.0f) * (float)config->sample_rate));
        if (s->smoothing_coeff > 1.0f) s->smoothing_coeff = 1.0f;
    }
    return ORPHEUS_OK;
}

static int switch_reset(void* state) {
    SwitchState* s = (SwitchState*)state;
    s->enable = 1.0f;
    s->gain = 1.0f;
    s->target = 1.0f;
    return ORPHEUS_OK;
}

static int switch_process(void* state, const OrpheusProcessContext* ctx) {
    SwitchState* s = (SwitchState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (in == NULL || out == NULL) return ORPHEUS_ERR_INVALID_ARG;

    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;
    s->target = s->enable >= 0.5f ? 1.0f : 0.0f;
    for (uint32_t i = 0; i < frames * ch; ++i) {
        s->gain += s->smoothing_coeff * (s->target - s->gain);
        out_data[i] = in_data[i] * s->gain;
    }
    out->frame_count = frames;
    return ORPHEUS_OK;
}

static int switch_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    SwitchState* s = (SwitchState*)state;
    if (strcmp(param_id, "enable") == 0) {
        if (value->type != ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        s->enable = value->value.f32;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int switch_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    SwitchState* s = (SwitchState*)state;
    if (strcmp(param_id, "enable") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT;
        value->value.f32 = s->enable;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)s->channels;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int switch_register_slots(void* state, const OrpheusRegistry* reg) {
    SwitchState* s = (SwitchState*)state;
    ORPHEUS_REG_SLOT(reg, s, enable, ORPHEUS_SLOT_SETTING, "enable", "使能",
                     ORPHEUS_VALUE_FLOAT, .min_f32=0.0f, .max_f32=1.0f,
                     .update_policy=ORPHEUS_UPDATE_SMOOTHED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_DIRECT_WRITE);
    ORPHEUS_REG_SLOT(reg, s, ramp_ms, ORPHEUS_SLOT_SETTING, "ramp_ms", "斜坡时间",
                     ORPHEUS_VALUE_FLOAT, .min_f32=0.0f, .max_f32=1000.0f, .unit="ms",
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface switch_interface = {
    .get_descriptor = switch_get_descriptor, .create = switch_create, .destroy = switch_destroy,
    .prepare = switch_prepare, .reset = switch_reset, .process = switch_process,
    .set_parameter = switch_set_parameter, .get_parameter = switch_get_parameter,
    .get_state_value = NULL, .register_slots = switch_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {
    return &switch_interface;
}
