#include "orpheus_interp_lut.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static const OrpheusParameter il_params[] = {
    { .id = "x_axis", .name = "X 轴", .type = ORPHEUS_VALUE_STRING,
      .default_value = { .type = ORPHEUS_VALUE_STRING, .value.str = "0, 1" },
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "y_axis", .name = "Y 轴", .type = ORPHEUS_VALUE_STRING,
      .default_value = { .type = ORPHEUS_VALUE_STRING, .value.str = "0, 1" },
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "x", .name = "输入值", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.0f },
      .min_f32 = -1e9f, .max_f32 = 1e9f, .update_policy = ORPHEUS_UPDATE_IMMEDIATE,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "y", .name = "输出值", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.0f },
      .update_policy = ORPHEUS_UPDATE_IMMEDIATE,
      .readback = true, .persistent = false, .affects_signature = false },
    { .id = "history", .name = "输出历史", .type = ORPHEUS_VALUE_STRING,
      .default_value = { .type = ORPHEUS_VALUE_STRING, .value.str = "[]" },
      .update_policy = ORPHEUS_UPDATE_IMMEDIATE,
      .readback = true, .persistent = false, .affects_signature = false },
    { .id = "channels", .name = "通道数", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 1 },
      .min_i32 = 1, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true }
};

static const OrpheusPort il_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor il_descriptor = {
    .id = "orpheus.builtin.interp_lut", .version = "1.0.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = il_ports, .port_count = 2, .params = il_params, .param_count = 6,
    .state_size = sizeof(InterpLutState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = true
};

static const OrpheusComponentDescriptor* il_get_descriptor(void) { return &il_descriptor; }

static int il_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(InterpLutState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int il_destroy(void* state) { (void)state; return ORPHEUS_OK; }

static const char* read_string(const OrpheusConfig* config, const char* id, const char* fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0 &&
            config->param_values[i].type == ORPHEUS_VALUE_STRING) {
            return config->param_values[i].value.str ? config->param_values[i].value.str : fallback;
        }
    }
    return fallback;
}

static int il_prepare(void* state, const OrpheusConfig* config) {
    InterpLutState* s = (InterpLutState*)state;
    s->channels = config->channels > 0 ? config->channels : 1;
    uint32_t nx = parse_floats(read_string(config, "x_axis", "0, 1"), s->axis, IL_MAX_POINTS);
    uint32_t ny = parse_floats(read_string(config, "y_axis", "0, 1"), s->table, IL_MAX_POINTS);
    s->count = nx < ny ? nx : ny;
    if (s->count == 0) { s->axis[0] = 0.0f; s->table[0] = 0.0f; s->count = 1; }
    s->x = 0.0f;
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], "x") == 0 &&
            config->param_values[i].type == ORPHEUS_VALUE_FLOAT) {
            s->x = config->param_values[i].value.f32;
        }
    }
    s->hist_pos = s->hist_count = 0;
    s->json_history[0] = '\0';
    return ORPHEUS_OK;
}

static int il_reset(void* state) {
    InterpLutState* s = (InterpLutState*)state;
    s->x = 0.0f;
    s->y = 0.0f;
    s->hist_pos = s->hist_count = 0;
    s->json_history[0] = '\0';
    return ORPHEUS_OK;
}

static float interp1(const float* axis, const float* table, uint32_t n, float x) {
    if (x <= axis[0]) return table[0];
    if (x >= axis[n - 1]) return table[n - 1];
    for (uint32_t i = 0; i + 1 < n; ++i) {
        if (x >= axis[i] && x <= axis[i + 1]) {
            float span = axis[i + 1] - axis[i];
            if (span <= 0.0f) return table[i];
            float t = (x - axis[i]) / span;
            return table[i] + t * (table[i + 1] - table[i]);
        }
    }
    return table[0];
}

static int il_process(void* state, const OrpheusProcessContext* ctx) {
    InterpLutState* s = (InterpLutState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (in == NULL || out == NULL) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    if (out != in) memcpy(out->data, in->data, frames * ch * sizeof(float));
    out->frame_count = frames;

    s->y = interp1(s->axis, s->table, s->count, s->x);
    s->hist[s->hist_pos] = s->y;
    s->hist_pos = (s->hist_pos + 1) % IL_HISTORY;
    if (s->hist_count < IL_HISTORY) s->hist_count++;
    char* p = s->json_history;
    size_t rem = sizeof(s->json_history);
    int len = snprintf(p, rem, "[");
    p += len; rem -= (size_t)len;
    for (uint32_t i = 0; i < s->hist_count && rem > 8; ++i) {
        uint32_t idx = (s->hist_pos + IL_HISTORY - s->hist_count + i) % IL_HISTORY;
        len = snprintf(p, rem, i ? ",%.4g" : "%.4g", s->hist[idx]);
        p += len; rem -= (size_t)len;
    }
    if (rem > 2) snprintf(p, rem, "]");
    return ORPHEUS_OK;
}

static int il_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    InterpLutState* s = (InterpLutState*)state;
    if (strcmp(param_id, "x") == 0) {
        if (value->type != ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        s->x = value->value.f32;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int il_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    InterpLutState* s = (InterpLutState*)state;
    if (strcmp(param_id, "x") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT;
        value->value.f32 = s->x;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "y") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT;
        value->value.f32 = s->y;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "history") == 0) {
        value->type = ORPHEUS_VALUE_STRING;
        value->value.str = s->json_history;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)s->channels;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int il_register_slots(void* state, const OrpheusRegistry* reg) {
    InterpLutState* s = (InterpLutState*)state;
    ORPHEUS_REG_SLOT(reg, s, x, ORPHEUS_SLOT_SETTING, "x", "输入值",
                     ORPHEUS_VALUE_FLOAT, .min_f32=-1e9f, .max_f32=1e9f,
                     .update_policy=ORPHEUS_UPDATE_IMMEDIATE,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_DIRECT_WRITE);
    ORPHEUS_REG_SLOT(reg, s, y, ORPHEUS_SLOT_PROBE, "y", "输出值",
                     ORPHEUS_VALUE_FLOAT, .flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, json_history, ORPHEUS_SLOT_PROBE, "history", "输出历史",
                     ORPHEUS_VALUE_STRING, .flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface il_interface = {
    .get_descriptor = il_get_descriptor, .create = il_create, .destroy = il_destroy,
    .prepare = il_prepare, .reset = il_reset, .process = il_process,
    .set_parameter = il_set_parameter, .get_parameter = il_get_parameter,
    .get_state_value = NULL, .register_slots = il_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {
    return &il_interface;
}
