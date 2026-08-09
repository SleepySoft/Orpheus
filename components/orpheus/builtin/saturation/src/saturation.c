#include "orpheus_saturation.h"

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

static const OrpheusParameter sat_params[] = {
    { .id = "limit", .name = "上限", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.5f },
      .min_f32 = 0.01f, .max_f32 = 1.0f, .update_policy = ORPHEUS_UPDATE_SMOOTHED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "soft", .name = "软饱和", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.0f },
      .min_f32 = 0.0f, .max_f32 = 1.0f, .update_policy = ORPHEUS_UPDATE_SMOOTHED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "channels", .name = "通道数", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true }
};

static const OrpheusPort sat_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor sat_descriptor = {
    .id = "orpheus.builtin.saturation", .version = "1.0.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = sat_ports, .port_count = 2, .params = sat_params, .param_count = 3,
    .state_size = sizeof(SaturationState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = true
};

static const OrpheusComponentDescriptor* sat_get_descriptor(void) { return &sat_descriptor; }

static int sat_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(SaturationState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int sat_destroy(void* state) { (void)state; return ORPHEUS_OK; }

static int sat_prepare(void* state, const OrpheusConfig* config) {
    SaturationState* s = (SaturationState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    s->limit = read_float(config, "limit", 0.5f);
    s->soft = read_float(config, "soft", 0.0f);
    return ORPHEUS_OK;
}

static int sat_reset(void* state) {
    SaturationState* s = (SaturationState*)state;
    s->limit = 0.5f;
    s->soft = 0.0f;
    return ORPHEUS_OK;
}

static int sat_process(void* state, const OrpheusProcessContext* ctx) {
    SaturationState* s = (SaturationState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (in == NULL || out == NULL) return ORPHEUS_ERR_INVALID_ARG;

    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;
    float limit = s->limit > 0.0f ? s->limit : 0.001f;
    float soft = s->soft < 0.0f ? 0.0f : (s->soft > 1.0f ? 1.0f : s->soft);
    for (uint32_t i = 0; i < frames * ch; ++i) {
        float x = in_data[i];
        float hard = x > limit ? limit : (x < -limit ? -limit : x);
        float soft_y = limit * tanhf(x / limit);
        out_data[i] = (1.0f - soft) * hard + soft * soft_y;
    }
    out->frame_count = frames;
    return ORPHEUS_OK;
}

static int sat_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    SaturationState* s = (SaturationState*)state;
    if (strcmp(param_id, "limit") == 0) {
        if (value->type != ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        s->limit = value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "soft") == 0) {
        if (value->type != ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        s->soft = value->value.f32;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int sat_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    SaturationState* s = (SaturationState*)state;
    if (strcmp(param_id, "limit") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT;
        value->value.f32 = s->limit;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "soft") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT;
        value->value.f32 = s->soft;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)s->channels;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int sat_register_slots(void* state, const OrpheusRegistry* reg) {
    SaturationState* s = (SaturationState*)state;
    ORPHEUS_REG_SLOT(reg, s, limit, ORPHEUS_SLOT_SETTING, "limit", "上限",
                     ORPHEUS_VALUE_FLOAT, .min_f32=0.01f, .max_f32=1.0f,
                     .update_policy=ORPHEUS_UPDATE_SMOOTHED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_DIRECT_WRITE);
    ORPHEUS_REG_SLOT(reg, s, soft, ORPHEUS_SLOT_SETTING, "soft", "软饱和",
                     ORPHEUS_VALUE_FLOAT, .min_f32=0.0f, .max_f32=1.0f,
                     .update_policy=ORPHEUS_UPDATE_SMOOTHED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_DIRECT_WRITE);
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface sat_interface = {
    .get_descriptor = sat_get_descriptor, .create = sat_create, .destroy = sat_destroy,
    .prepare = sat_prepare, .reset = sat_reset, .process = sat_process,
    .set_parameter = sat_set_parameter, .get_parameter = sat_get_parameter,
    .get_state_value = NULL, .register_slots = sat_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {
    return &sat_interface;
}
