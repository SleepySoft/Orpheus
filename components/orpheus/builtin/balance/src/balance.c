#include "orpheus_balance.h"

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

static void balance_targets(float balance, float* tl, float* tr) {
    if (balance >= 0.0f) { *tl = 1.0f - balance; *tr = 1.0f; }
    else                 { *tl = 1.0f; *tr = 1.0f + balance; }
}

static const OrpheusParameter bal_params[] = {
    { .id = "balance", .name = "Balance", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.0f },
      .min_f32 = -1.0f, .max_f32 = 1.0f, .update_policy = ORPHEUS_UPDATE_SMOOTHED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "channels", .name = "Channels", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 2, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "ramp_ms", .name = "Ramp Time", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 50.0f },
      .min_f32 = 0.0f, .max_f32 = 1000.0f, .unit = "ms", .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = false }
};

static const OrpheusPort bal_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor bal_descriptor = {
    .id = "orpheus.builtin.balance", .version = "1.0.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = bal_ports, .port_count = 2, .params = bal_params, .param_count = 3,
    .state_size = sizeof(BalanceState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = true
};

static const OrpheusComponentDescriptor* bal_get_descriptor(void) { return &bal_descriptor; }

static int bal_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(BalanceState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int bal_destroy(void* state) { (void)state; return ORPHEUS_OK; } /* v2：内存由 Runtime 统一管理 */

static int bal_prepare(void* state, const OrpheusConfig* config) {
    BalanceState* s = (BalanceState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    s->balance = read_float(config, "balance", 0.0f);
    float ramp_ms = read_float(config, "ramp_ms", 50.0f);
    balance_targets(s->balance, &s->target_left, &s->target_right);
    s->left_gain = s->target_left;
    s->right_gain = s->target_right;
    if (ramp_ms <= 0.0f || config->sample_rate == 0) {
        s->smoothing_coeff = 1.0f;
    } else {
        s->smoothing_coeff = 1.0f - expf(-1.0f / ((ramp_ms / 1000.0f) * (float)config->sample_rate));
        if (s->smoothing_coeff > 1.0f) s->smoothing_coeff = 1.0f;
    }
    s->ramp_ms = ramp_ms;
    return ORPHEUS_OK;
}
static int bal_reset(void* state) {
    BalanceState* s = (BalanceState*)state;
    s->left_gain = s->target_left;
    s->right_gain = s->target_right;
    return ORPHEUS_OK;
}
static int bal_process(void* state, const OrpheusProcessContext* ctx) {
    BalanceState* s = (BalanceState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (in == NULL || out == NULL) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;
    for (uint32_t n = 0; n < frames; ++n) {
        s->left_gain += s->smoothing_coeff * (s->target_left - s->left_gain);
        s->right_gain += s->smoothing_coeff * (s->target_right - s->right_gain);
        for (uint32_t c = 0; c < ch; ++c) {
            float g = (c % 2u == 0u) ? s->left_gain : s->right_gain;
            out_data[n * ch + c] = in_data[n * ch + c] * g;
        }
    }
    out->frame_count = frames;
    return ORPHEUS_OK;
}
static int bal_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    BalanceState* s = (BalanceState*)state;
    if (strcmp(param_id, "balance") == 0) {
        if (value->type != ORPHEUS_VALUE_FLOAT && value->type != ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        s->balance = (value->type == ORPHEUS_VALUE_FLOAT) ? value->value.f32 : (float)value->value.i32;
        if (s->balance < -1.0f) s->balance = -1.0f;
        if (s->balance > 1.0f) s->balance = 1.0f;
        balance_targets(s->balance, &s->target_left, &s->target_right);
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int bal_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    BalanceState* s = (BalanceState*)state;
    if (strcmp(param_id, "balance") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT; value->value.f32 = s->balance; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->channels; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int bal_register_slots(void* state, const OrpheusRegistry* reg) {
    BalanceState* s = (BalanceState*)state;
    ORPHEUS_REG_SLOT(reg, s, balance, ORPHEUS_SLOT_SETTING, "balance", "平衡",
                     ORPHEUS_VALUE_FLOAT, .min_f32=-1.0f, .max_f32=1.0f,
                     .update_policy=ORPHEUS_UPDATE_SMOOTHED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, ramp_ms, ORPHEUS_SLOT_SETTING, "ramp_ms", "斜坡时间",
                     ORPHEUS_VALUE_FLOAT, .min_f32=0.0f, .max_f32=1000.0f, .unit="ms",
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=2, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    return ORPHEUS_OK;
}
static const OrpheusComponentInterface bal_interface = {
    .get_descriptor = bal_get_descriptor, .create = bal_create, .destroy = bal_destroy,
    .prepare = bal_prepare, .reset = bal_reset, .process = bal_process,
    .set_parameter = bal_set_parameter, .get_parameter = bal_get_parameter, .get_state_value = NULL,
    .register_slots = bal_register_slots
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &bal_interface; }
