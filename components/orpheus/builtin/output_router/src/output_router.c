#include "orpheus_output_router.h"

#include <stdlib.h>
#include <string.h>

static uint32_t read_uint(const OrpheusConfig* config, const char* id, uint32_t fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_FLOAT) return (uint32_t)config->param_values[i].value.f32;
            if (config->param_values[i].type == ORPHEUS_VALUE_INT) return (uint32_t)config->param_values[i].value.i32;
        }
    }
    return fallback;
}

static const char* read_str(const OrpheusConfig* config, const char* id, const char* fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_STRING) return config->param_values[i].value.str;
        }
    }
    return fallback;
}

static const OrpheusParameter or_params[] = {
    { .id = "channels_in", .name = "Input Channels", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "channels_out", .name = "Output Channels", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "matrix", .name = "Matrix", .type = ORPHEUS_VALUE_STRING,
      .default_value = { .type = ORPHEUS_VALUE_STRING, .value.str = "identity" },
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = false }
};

static const OrpheusPort or_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels_in" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels_out" }
};

static const OrpheusComponentDescriptor or_descriptor = {
    .id = "orpheus.builtin.output_router", .version = "1.0.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = or_ports, .port_count = 2, .params = or_params, .param_count = 3,
    .state_size = sizeof(OutputRouterState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = false
};

static const OrpheusComponentDescriptor* or_get_descriptor(void) { return &or_descriptor; }

static int or_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(OutputRouterState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int or_destroy(void* state) { (void)state; return ORPHEUS_OK; } /* v2：内存由 Runtime 统一管理 */

static int or_prepare(void* state, const OrpheusConfig* config) {
    OutputRouterState* s = (OutputRouterState*)state;
    s->channels_in = read_uint(config, "channels_in", 2);
    s->channels_out = read_uint(config, "channels_out", 2);
    if (s->channels_in > MAX_CH) s->channels_in = MAX_CH;
    if (s->channels_out > MAX_CH) s->channels_out = MAX_CH;
    uint32_t cin = s->channels_in;
    uint32_t cout = s->channels_out;
    /* default diagonal passthrough: out[o] <- in[o] when o < both */
    for (uint32_t i = 0; i < MAX_CH * MAX_CH; ++i) s->matrix[i] = 0.0f;
    for (uint32_t o = 0; o < cout && o < cin; ++o) s->matrix[o * MAX_CH + o] = 1.0f;

    const char* str = read_str(config, "matrix", NULL);
    if (str != NULL && strcmp(str, "identity") != 0) {
        char buf[2048];
        size_t len = strlen(str);
        if (len >= sizeof(buf)) len = sizeof(buf) - 1;
        memcpy(buf, str, len);
        buf[len] = '\0';
        uint32_t total = cin * cout;
        uint32_t idx = 0;
        char* p = buf;
        while (p != NULL && *p != '\0' && idx < total) {
            char* end = NULL;
            float val = strtof(p, &end);
            if (end == p) { ++p; continue; }
            s->matrix[(idx / cin) * MAX_CH + (idx % cin)] = val;
            ++idx;
            p = end;
        }
    }
    const char* ms = str != NULL ? str : "identity";
    strncpy(s->matrix_str, ms, sizeof(s->matrix_str) - 1);
    s->matrix_str[sizeof(s->matrix_str) - 1] = '\0';
    return ORPHEUS_OK;
}
static int or_reset(void* state) { (void)state; return ORPHEUS_OK; }

static int or_process(void* state, const OrpheusProcessContext* ctx) {
    OutputRouterState* s = (OutputRouterState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (in == NULL || out == NULL) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t frames = ctx->frame_count;
    uint32_t cin = in->channels;
    uint32_t cout = out->channels;
    if (cin > MAX_CH) cin = MAX_CH;
    if (cout > MAX_CH) cout = MAX_CH;
    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;
    for (uint32_t n = 0; n < frames; ++n) {
        for (uint32_t o = 0; o < cout; ++o) {
            float acc = 0.0f;
            for (uint32_t i = 0; i < cin; ++i) {
                float g = s->matrix[o * MAX_CH + i];
                if (g != 0.0f) acc += g * in_data[n * cin + i];
            }
            out_data[n * cout + o] = acc;
        }
    }
    out->frame_count = frames;
    return ORPHEUS_OK;
}
static int or_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    (void)state; (void)param_id; (void)value;
    return ORPHEUS_ERR_UNSUPPORTED;
}
static int or_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    OutputRouterState* s = (OutputRouterState*)state;
    if (strcmp(param_id, "channels_in") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->channels_in; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "channels_out") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->channels_out; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int or_register_slots(void* state, const OrpheusRegistry* reg) {
    OutputRouterState* s = (OutputRouterState*)state;
    ORPHEUS_REG_SLOT(reg, s, channels_in, ORPHEUS_SLOT_SETTING, "channels_in", "输入通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, channels_out, ORPHEUS_SLOT_SETTING, "channels_out", "输出通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, matrix_str, ORPHEUS_SLOT_SETTING, "matrix", "路由矩阵",
                     ORPHEUS_VALUE_STRING, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    return ORPHEUS_OK;
}
static const OrpheusComponentInterface or_interface = {
    .get_descriptor = or_get_descriptor, .create = or_create, .destroy = or_destroy,
    .prepare = or_prepare, .reset = or_reset, .process = or_process,
    .set_parameter = or_set_parameter, .get_parameter = or_get_parameter, .get_state_value = NULL,
    .register_slots = or_register_slots
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &or_interface; }
