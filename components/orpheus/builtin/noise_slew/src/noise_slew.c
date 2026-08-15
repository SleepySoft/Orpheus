#include "orpheus_noise_slew.h"

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

static const OrpheusParameter ns_params[] = {
    { .id = "rise_rate", .name = "上升速率", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 1.0f },
      .min_f32 = 0.0001f, .max_f32 = 100.0f, .unit = "/s",
      .update_policy = ORPHEUS_UPDATE_SMOOTHED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "fall_rate", .name = "下降速率", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 1.0f },
      .min_f32 = 0.0001f, .max_f32 = 100.0f, .unit = "/s",
      .update_policy = ORPHEUS_UPDATE_SMOOTHED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "channels", .name = "通道数", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true }
};

static const OrpheusPort ns_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor ns_descriptor = {
    .id = "orpheus.builtin.noise_slew", .version = "1.0.1", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = ns_ports, .port_count = 2, .params = ns_params, .param_count = 3,
    .state_size = sizeof(NoiseSlewState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = true
};

static const OrpheusComponentDescriptor* ns_get_descriptor(void) { return &ns_descriptor; }

static int ns_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(NoiseSlewState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int ns_destroy(void* state) { (void)state; return ORPHEUS_OK; }

static int ns_prepare(void* state, const OrpheusConfig* config) {
    NoiseSlewState* s = (NoiseSlewState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    if (s->channels > 32) s->channels = 32;  /* prev[] 定长 32，钳制防越界 */
    s->rise_rate = read_float(config, "rise_rate", 1.0f);
    s->fall_rate = read_float(config, "fall_rate", 1.0f);
    s->rise_delta = config->sample_rate > 0 ? s->rise_rate / (float)config->sample_rate : 0.0001f;
    s->fall_delta = config->sample_rate > 0 ? s->fall_rate / (float)config->sample_rate : 0.0001f;
    memset(s->prev, 0, sizeof(s->prev));
    return ORPHEUS_OK;
}

static int ns_reset(void* state) {
    NoiseSlewState* s = (NoiseSlewState*)state;
    memset(s->prev, 0, sizeof(s->prev));
    return ORPHEUS_OK;
}

static int ns_process(void* state, const OrpheusProcessContext* ctx) {
    NoiseSlewState* s = (NoiseSlewState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (in == NULL || out == NULL) return ORPHEUS_ERR_INVALID_ARG;

    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;
    for (uint32_t i = 0; i < frames * ch; ++i) {
        uint32_t c = i % ch;
        float x = in_data[i];
        float delta = x - s->prev[c];
        float maxd = delta > 0.0f ? s->rise_delta : s->fall_delta;
        if (delta > maxd) delta = maxd;
        else if (delta < -maxd) delta = -maxd;
        float y = s->prev[c] + delta;
        s->prev[c] = y;
        out_data[i] = y;
    }
    out->frame_count = frames;
    return ORPHEUS_OK;
}

static int ns_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    NoiseSlewState* s = (NoiseSlewState*)state;
    if (strcmp(param_id, "rise_rate") == 0) {
        if (value->type != ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        s->rise_rate = value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "fall_rate") == 0) {
        if (value->type != ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        s->fall_rate = value->value.f32;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int ns_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    NoiseSlewState* s = (NoiseSlewState*)state;
    if (strcmp(param_id, "rise_rate") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT;
        value->value.f32 = s->rise_rate;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "fall_rate") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT;
        value->value.f32 = s->fall_rate;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)s->channels;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int ns_register_slots(void* state, const OrpheusRegistry* reg) {
    NoiseSlewState* s = (NoiseSlewState*)state;
    ORPHEUS_REG_SLOT(reg, s, rise_rate, ORPHEUS_SLOT_SETTING, "rise_rate", "上升速率",
                     ORPHEUS_VALUE_FLOAT, .min_f32=0.0001f, .max_f32=100.0f, .unit="/s",
                     .update_policy=ORPHEUS_UPDATE_SMOOTHED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_DIRECT_WRITE);
    ORPHEUS_REG_SLOT(reg, s, fall_rate, ORPHEUS_SLOT_SETTING, "fall_rate", "下降速率",
                     ORPHEUS_VALUE_FLOAT, .min_f32=0.0001f, .max_f32=100.0f, .unit="/s",
                     .update_policy=ORPHEUS_UPDATE_SMOOTHED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_DIRECT_WRITE);
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface ns_interface = {
    .get_descriptor = ns_get_descriptor, .create = ns_create, .destroy = ns_destroy,
    .prepare = ns_prepare, .reset = ns_reset, .process = ns_process,
    .set_parameter = ns_set_parameter, .get_parameter = ns_get_parameter,
    .get_state_value = NULL, .register_slots = ns_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {
    return &ns_interface;
}
