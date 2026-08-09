#include "orpheus_limiter.h"

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

static float db_to_linear(float db) { return powf(10.0f, db / 20.0f); }

static float ms_to_coeff(float ms, uint32_t sample_rate) {
    if (ms <= 0.0f || sample_rate == 0) return 1.0f;
    float c = 1.0f - expf(-1.0f / ((ms / 1000.0f) * (float)sample_rate));
    return c > 1.0f ? 1.0f : c;
}

static const OrpheusParameter limiter_params[] = {
    { .id = "threshold_db", .name = "阈值", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = -6.0f },
      .min_f32 = -60.0f, .max_f32 = 0.0f, .unit = "dB",
      .update_policy = ORPHEUS_UPDATE_SMOOTHED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "attack_ms", .name = "启动时间", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 5.0f },
      .min_f32 = 0.1f, .max_f32 = 1000.0f, .unit = "ms",
      .update_policy = ORPHEUS_UPDATE_SMOOTHED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "release_ms", .name = "释放时间", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 100.0f },
      .min_f32 = 1.0f, .max_f32 = 5000.0f, .unit = "ms",
      .update_policy = ORPHEUS_UPDATE_SMOOTHED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "channels", .name = "通道数", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true }
};

static const OrpheusPort limiter_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor limiter_descriptor = {
    .id = "orpheus.builtin.limiter", .version = "1.0.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = limiter_ports, .port_count = 2, .params = limiter_params, .param_count = 4,
    .state_size = sizeof(LimiterState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = true
};

static const OrpheusComponentDescriptor* limiter_get_descriptor(void) { return &limiter_descriptor; }

static int limiter_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(LimiterState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int limiter_destroy(void* state) { (void)state; return ORPHEUS_OK; }

static int limiter_prepare(void* state, const OrpheusConfig* config) {
    LimiterState* s = (LimiterState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    s->threshold_db = read_float(config, "threshold_db", -6.0f);
    s->attack_ms = read_float(config, "attack_ms", 5.0f);
    s->release_ms = read_float(config, "release_ms", 100.0f);
    s->threshold_linear = db_to_linear(s->threshold_db);
    s->attack_coeff = ms_to_coeff(s->attack_ms, config->sample_rate);
    s->release_coeff = ms_to_coeff(s->release_ms, config->sample_rate);
    s->env = 0.0f;
    s->gain = 1.0f;
    return ORPHEUS_OK;
}

static int limiter_reset(void* state) {
    LimiterState* s = (LimiterState*)state;
    s->env = 0.0f;
    s->gain = 1.0f;
    return ORPHEUS_OK;
}

static int limiter_process(void* state, const OrpheusProcessContext* ctx) {
    LimiterState* s = (LimiterState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (in == NULL || out == NULL) return ORPHEUS_ERR_INVALID_ARG;

    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;
    s->threshold_linear = db_to_linear(s->threshold_db);

    for (uint32_t i = 0; i < frames * ch; ++i) {
        float x = in_data[i];
        float abs_x = x >= 0.0f ? x : -x;
        if (abs_x > s->env) {
            s->env += s->attack_coeff * (abs_x - s->env);
        } else {
            s->env += s->release_coeff * (abs_x - s->env);
        }
        float g = (s->env > s->threshold_linear && s->env > 0.0f)
                      ? s->threshold_linear / s->env
                      : 1.0f;
        s->gain = g;
        out_data[i] = x * g;
    }
    out->frame_count = frames;
    return ORPHEUS_OK;
}

static int limiter_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    LimiterState* s = (LimiterState*)state;
    if (strcmp(param_id, "threshold_db") == 0) {
        if (value->type != ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        s->threshold_db = value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "attack_ms") == 0) {
        if (value->type != ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        s->attack_ms = value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "release_ms") == 0) {
        if (value->type != ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        s->release_ms = value->value.f32;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int limiter_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    LimiterState* s = (LimiterState*)state;
    if (strcmp(param_id, "threshold_db") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT;
        value->value.f32 = s->threshold_db;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)s->channels;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "gain") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT;
        value->value.f32 = s->gain;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int limiter_register_slots(void* state, const OrpheusRegistry* reg) {
    LimiterState* s = (LimiterState*)state;
    ORPHEUS_REG_SLOT(reg, s, threshold_db, ORPHEUS_SLOT_SETTING, "threshold_db", "阈值",
                     ORPHEUS_VALUE_FLOAT, .min_f32=-60.0f, .max_f32=0.0f, .unit="dB",
                     .update_policy=ORPHEUS_UPDATE_SMOOTHED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_DIRECT_WRITE);
    ORPHEUS_REG_SLOT(reg, s, attack_ms, ORPHEUS_SLOT_SETTING, "attack_ms", "启动时间",
                     ORPHEUS_VALUE_FLOAT, .min_f32=0.1f, .max_f32=1000.0f, .unit="ms",
                     .update_policy=ORPHEUS_UPDATE_SMOOTHED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_DIRECT_WRITE);
    ORPHEUS_REG_SLOT(reg, s, release_ms, ORPHEUS_SLOT_SETTING, "release_ms", "释放时间",
                     ORPHEUS_VALUE_FLOAT, .min_f32=1.0f, .max_f32=5000.0f, .unit="ms",
                     .update_policy=ORPHEUS_UPDATE_SMOOTHED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_DIRECT_WRITE);
    ORPHEUS_REG_SLOT(reg, s, gain, ORPHEUS_SLOT_PROBE, "gain", "当前增益",
                     ORPHEUS_VALUE_FLOAT, .flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface limiter_interface = {
    .get_descriptor = limiter_get_descriptor, .create = limiter_create, .destroy = limiter_destroy,
    .prepare = limiter_prepare, .reset = limiter_reset, .process = limiter_process,
    .set_parameter = limiter_set_parameter, .get_parameter = limiter_get_parameter,
    .get_state_value = NULL, .register_slots = limiter_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {
    return &limiter_interface;
}
