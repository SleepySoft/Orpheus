#include "orpheus_matrix_mul.h"

#include <math.h>
#include <stdio.h>
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

static uint32_t parse_matrix(const char* text, float* out, uint32_t max_count) {
    if (text == NULL) return 0;
    uint32_t count = 0;
    const char* p = text;
    while (*p && count < max_count) {
        out[count++] = (float)strtod(p, (char**)&p);
        while (*p == ',' || *p == ' ' || *p == '\t' || *p == '\n') ++p;
    }
    return count;
}

static const OrpheusParameter mm_params[] = {
    { .id = "rows", .name = "输出行数", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "cols", .name = "输入列数", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "matrix", .name = "矩阵系数", .type = ORPHEUS_VALUE_STRING,
      .default_value = { .type = ORPHEUS_VALUE_STRING, .value.str = "1, 0, 0, 1" },
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = false }
};

static const OrpheusPort mm_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "cols" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "rows" }
};

static const OrpheusComponentDescriptor mm_descriptor = {
    .id = "orpheus.builtin.matrix_mul", .version = "1.0.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = mm_ports, .port_count = 2, .params = mm_params, .param_count = 3,
    .state_size = sizeof(MatrixMulState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = false
};

static const OrpheusComponentDescriptor* mm_get_descriptor(void) { return &mm_descriptor; }

static int mm_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(MatrixMulState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int mm_destroy(void* state) { (void)state; return ORPHEUS_OK; }

static int mm_prepare(void* state, const OrpheusConfig* config) {
    MatrixMulState* s = (MatrixMulState*)state;
    s->rows = (uint32_t)read_float(config, "rows", 2.0f);
    s->cols = (uint32_t)read_float(config, "cols", 2.0f);
    if (s->rows < 1 || s->rows > 32) s->rows = 2;
    if (s->cols < 1 || s->cols > 32) s->cols = 2;
    memset(s->matrix, 0, sizeof(s->matrix));
    uint32_t n = parse_matrix(read_string(config, "matrix", "1, 0, 0, 1"), s->matrix, ORPHEUS_MATRIX_MAX);
    if (n == 0) {
        for (uint32_t i = 0; i < s->rows && i < s->cols; ++i) s->matrix[i * s->cols + i] = 1.0f;
    }
    return ORPHEUS_OK;
}

static int mm_reset(void* state) {
    MatrixMulState* s = (MatrixMulState*)state;
    memset(s->matrix, 0, sizeof(s->matrix));
    for (uint32_t i = 0; i < s->rows && i < s->cols; ++i) s->matrix[i * s->cols + i] = 1.0f;
    return ORPHEUS_OK;
}

static int mm_process(void* state, const OrpheusProcessContext* ctx) {
    MatrixMulState* s = (MatrixMulState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (in == NULL || out == NULL) return ORPHEUS_ERR_INVALID_ARG;

    uint32_t frames = ctx->frame_count;
    uint32_t rows = s->rows;
    uint32_t cols = s->cols;
    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;
    for (uint32_t f = 0; f < frames; ++f) {
        const float* x = in_data + f * cols;
        float* y = out_data + f * rows;
        for (uint32_t r = 0; r < rows; ++r) {
            float acc = 0.0f;
            const float* row = s->matrix + r * cols;
            for (uint32_t c = 0; c < cols; ++c) acc += row[c] * x[c];
            y[r] = acc;
        }
    }
    out->frame_count = frames;
    return ORPHEUS_OK;
}

static int mm_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    MatrixMulState* s = (MatrixMulState*)state;
    if (strcmp(param_id, "rows") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)s->rows;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "cols") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)s->cols;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "matrix") == 0) {
        value->type = ORPHEUS_VALUE_STRING;
        value->value.str = (const char*)s->matrix; /* 调用方按 float 解释；字符串仅占位 */
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int mm_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    (void)state; (void)param_id; (void)value;
    return ORPHEUS_ERR_NOT_FOUND;
}

static int mm_register_slots(void* state, const OrpheusRegistry* reg) {
    MatrixMulState* s = (MatrixMulState*)state;
    ORPHEUS_REG_SLOT(reg, s, rows, ORPHEUS_SLOT_SETTING, "rows", "输出行数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, cols, ORPHEUS_SLOT_SETTING, "cols", "输入列数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface mm_interface = {
    .get_descriptor = mm_get_descriptor, .create = mm_create, .destroy = mm_destroy,
    .prepare = mm_prepare, .reset = mm_reset, .process = mm_process,
    .set_parameter = mm_set_parameter, .get_parameter = mm_get_parameter,
    .get_state_value = NULL, .register_slots = mm_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {
    return &mm_interface;
}
