#include "orpheus_window.h"

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

static const char* read_string(const OrpheusConfig* config, const char* id, const char* fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0 &&
            config->param_values[i].type == ORPHEUS_VALUE_STRING) {
            return config->param_values[i].value.str ? config->param_values[i].value.str : fallback;
        }
    }
    return fallback;
}

static uint32_t parse_floats(const char* text, float* out, uint32_t max_count) {
    if (text == NULL) return 0;
    uint32_t count = 0;
    const char* p = text;
    while (*p && count < max_count) {
        out[count++] = (float)strtod(p, (char**)&p);
        while (*p == ',' || *p == ' ' || *p == '\t' || *p == '\n') ++p;
    }
    return count;
}

static const OrpheusParameter win_params[] = {
    { .id = "window_size", .name = "窗长", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 256 },
      .min_i32 = 2, .max_i32 = 4096, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "coefficients", .name = "系数", .type = ORPHEUS_VALUE_STRING,
      .default_value = { .type = ORPHEUS_VALUE_STRING, .value.str = "1.0" },
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "channels", .name = "通道数", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "mode", .name = "模式", .type = ORPHEUS_VALUE_STRING,
      .default_value = { .type = ORPHEUS_VALUE_STRING, .value.str = "single" },
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = false }
};

static const OrpheusPort win_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor win_descriptor = {
    .id = "orpheus.builtin.window", .version = "1.0.1", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = win_ports, .port_count = 2, .params = win_params, .param_count = 4,
    .state_size = sizeof(WindowState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = true
};

static const OrpheusComponentDescriptor* win_get_descriptor(void) { return &win_descriptor; }

static int win_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(WindowState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int win_destroy(void* state) { (void)state; return ORPHEUS_OK; }

static int win_prepare(void* state, const OrpheusConfig* config) {
    WindowState* s = (WindowState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    s->window_size = (uint32_t)read_float(config, "window_size", 256.0f);
    if (s->window_size < 2 || s->window_size > ORPHEUS_WINDOW_MAX) s->window_size = 256;
    s->mode = 0;
    const char* mode_str = read_string(config, "mode", "single");
    if (mode_str && strcmp(mode_str, "repeat") == 0) s->mode = 1;
    memset(s->coeffs, 0, sizeof(s->coeffs));
    uint32_t n = parse_floats(read_string(config, "coefficients", "1.0"), s->coeffs, s->window_size);
    if (n == 0) s->coeffs[0] = 1.0f;
    return ORPHEUS_OK;
}

static int win_reset(void* state) {
    WindowState* s = (WindowState*)state;
    memset(s->coeffs, 0, sizeof(s->coeffs));
    s->coeffs[0] = 1.0f;
    s->mode = 0;
    return ORPHEUS_OK;
}

static int win_process(void* state, const OrpheusProcessContext* ctx) {
    WindowState* s = (WindowState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (in == NULL || out == NULL) return ORPHEUS_ERR_INVALID_ARG;

    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    uint32_t size = s->window_size;
    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;
    /* 每块从头应用窗；超窗长部分直通。repeat 模式按 window_size 周期重复 */
    for (uint32_t f = 0; f < frames; ++f) {
        float w;
        if (s->mode == 1) {
            w = s->coeffs[f % size];
        } else {
            w = f < size ? s->coeffs[f] : 1.0f;
        }
        for (uint32_t c = 0; c < ch; ++c) out_data[f * ch + c] = in_data[f * ch + c] * w;
    }
    out->frame_count = frames;
    return ORPHEUS_OK;
}

static int win_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    WindowState* s = (WindowState*)state;
    if (strcmp(param_id, "window_size") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)s->window_size;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)s->channels;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "mode") == 0) {
        value->type = ORPHEUS_VALUE_STRING;
        value->value.str = s->mode == 1 ? "repeat" : "single";
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int win_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    (void)state; (void)param_id; (void)value;
    return ORPHEUS_ERR_NOT_FOUND;
}

static int win_register_slots(void* state, const OrpheusRegistry* reg) {
    WindowState* s = (WindowState*)state;
    ORPHEUS_REG_SLOT(reg, s, window_size, ORPHEUS_SLOT_SETTING, "window_size", "窗长",
                     ORPHEUS_VALUE_INT, .min_i32=2, .max_i32=4096,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, mode, ORPHEUS_SLOT_SETTING, "mode", "模式",
                     ORPHEUS_VALUE_STRING,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface win_interface = {
    .get_descriptor = win_get_descriptor, .create = win_create, .destroy = win_destroy,
    .prepare = win_prepare, .reset = win_reset, .process = win_process,
    .set_parameter = win_set_parameter, .get_parameter = win_get_parameter,
    .get_state_value = NULL, .register_slots = win_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {
    return &win_interface;
}
