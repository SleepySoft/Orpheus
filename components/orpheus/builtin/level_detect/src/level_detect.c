#include "orpheus_level_detect.h"

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

static float ms_to_coeff(float ms, uint32_t sample_rate) {
    if (ms <= 0.0f || sample_rate == 0) return 1.0f;
    float c = 1.0f - expf(-1.0f / ((ms / 1000.0f) * (float)sample_rate));
    return c > 1.0f ? 1.0f : c;
}

static const OrpheusParameter ld_params[] = {
    { .id = "mode", .name = "模式", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 0 },
      .min_i32 = 0, .max_i32 = 1, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "attack_ms", .name = "启动时间", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 10.0f },
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
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "level", .name = "电平", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.0f },
      .update_policy = ORPHEUS_UPDATE_IMMEDIATE,
      .readback = true, .persistent = false, .affects_signature = false }
};

static const OrpheusPort ld_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor ld_descriptor = {
    .id = "orpheus.builtin.level_detect", .version = "1.0.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = ld_ports, .port_count = 2, .params = ld_params, .param_count = 5,
    .state_size = sizeof(LevelDetectState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = true
};

static const OrpheusComponentDescriptor* ld_get_descriptor(void) { return &ld_descriptor; }

static int ld_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(LevelDetectState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int ld_destroy(void* state) { (void)state; return ORPHEUS_OK; }

static int ld_prepare(void* state, const OrpheusConfig* config) {
    LevelDetectState* s = (LevelDetectState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    s->mode = (int32_t)read_float(config, "mode", 0.0f);
    s->attack_ms = read_float(config, "attack_ms", 10.0f);
    s->release_ms = read_float(config, "release_ms", 100.0f);
    s->attack_coeff = ms_to_coeff(s->attack_ms, config->sample_rate);
    s->release_coeff = ms_to_coeff(s->release_ms, config->sample_rate);
    memset(s->env, 0, sizeof(s->env));
    s->level = 0.0f;
    return ORPHEUS_OK;
}

static int ld_reset(void* state) {
    LevelDetectState* s = (LevelDetectState*)state;
    memset(s->env, 0, sizeof(s->env));
    s->level = 0.0f;
    return ORPHEUS_OK;
}

static int ld_process(void* state, const OrpheusProcessContext* ctx) {
    LevelDetectState* s = (LevelDetectState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (in == NULL || out == NULL) return ORPHEUS_ERR_INVALID_ARG;

    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;
    float max_env = 0.0f;
    if (s->mode == 1) {
        /* RMS：每块先算各通道 RMS，再平滑包络 */
        float block_rms[32] = {0};
        for (uint32_t f = 0; f < frames; ++f) {
            for (uint32_t c = 0; c < ch; ++c) {
                float x = in_data[f * ch + c];
                block_rms[c] += x * x;
            }
        }
        for (uint32_t c = 0; c < ch; ++c) {
            float rms = sqrtf(block_rms[c] / (float)frames);
            s->env[c] += s->attack_coeff * (rms - s->env[c]);
            if (s->env[c] > max_env) max_env = s->env[c];
        }
    } else {
        for (uint32_t i = 0; i < frames * ch; ++i) {
            uint32_t c = i % ch;
            float x = in_data[i];
            float abs_x = x >= 0.0f ? x : -x;
            if (abs_x > s->env[c]) {
                s->env[c] += s->attack_coeff * (abs_x - s->env[c]);
            } else {
                s->env[c] += s->release_coeff * (abs_x - s->env[c]);
            }
            if (s->env[c] > max_env) max_env = s->env[c];
        }
    }
    s->level = max_env;
    if (out != in) memcpy(out_data, in_data, frames * ch * sizeof(float));
    out->frame_count = frames;
    return ORPHEUS_OK;
}

static int ld_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    LevelDetectState* s = (LevelDetectState*)state;
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

static int ld_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    LevelDetectState* s = (LevelDetectState*)state;
    if (strcmp(param_id, "level") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT;
        value->value.f32 = s->level;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "mode") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = s->mode;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)s->channels;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int ld_register_slots(void* state, const OrpheusRegistry* reg) {
    LevelDetectState* s = (LevelDetectState*)state;
    ORPHEUS_REG_SLOT(reg, s, mode, ORPHEUS_SLOT_SETTING, "mode", "模式",
                     ORPHEUS_VALUE_INT, .min_i32=0, .max_i32=1,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, attack_ms, ORPHEUS_SLOT_SETTING, "attack_ms", "启动时间",
                     ORPHEUS_VALUE_FLOAT, .min_f32=0.1f, .max_f32=1000.0f, .unit="ms",
                     .update_policy=ORPHEUS_UPDATE_SMOOTHED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_DIRECT_WRITE);
    ORPHEUS_REG_SLOT(reg, s, release_ms, ORPHEUS_SLOT_SETTING, "release_ms", "释放时间",
                     ORPHEUS_VALUE_FLOAT, .min_f32=1.0f, .max_f32=5000.0f, .unit="ms",
                     .update_policy=ORPHEUS_UPDATE_SMOOTHED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_DIRECT_WRITE);
    ORPHEUS_REG_SLOT(reg, s, level, ORPHEUS_SLOT_PROBE, "level", "电平",
                     ORPHEUS_VALUE_FLOAT, .flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface ld_interface = {
    .get_descriptor = ld_get_descriptor, .create = ld_create, .destroy = ld_destroy,
    .prepare = ld_prepare, .reset = ld_reset, .process = ld_process,
    .set_parameter = ld_set_parameter, .get_parameter = ld_get_parameter,
    .get_state_value = NULL, .register_slots = ld_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {
    return &ld_interface;
}
