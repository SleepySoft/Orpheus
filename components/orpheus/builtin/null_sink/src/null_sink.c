#include "orpheus_null_sink.h"

#include <stdlib.h>
#include <string.h>

static int32_t read_int(const OrpheusConfig* config, const char* id, int32_t fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_INT)   return config->param_values[i].value.i32;
            if (config->param_values[i].type == ORPHEUS_VALUE_FLOAT) return (int32_t)config->param_values[i].value.f32;
        }
    }
    return fallback;
}

static const OrpheusParameter ns_params[] = {
    { .id = "channels", .name = "通道数", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32,
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true }
};

static const OrpheusPort ns_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor ns_descriptor = {
    .id = "orpheus.builtin.null_sink", .version = "1.0.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = ns_ports, .port_count = 1, .params = ns_params, .param_count = 1,
    .state_size = sizeof(NullSinkState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = true
};

static const OrpheusComponentDescriptor* ns_get_descriptor(void) { return &ns_descriptor; }

static int ns_create(void** state, const OrpheusConfig* config) {
    (void)config;
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(NullSinkState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}

static int ns_destroy(void* state) { (void)state; return ORPHEUS_OK; }

static int ns_prepare(void* state, const OrpheusConfig* config) {
    NullSinkState* s = (NullSinkState*)state;
    s->channels = config->channels > 0 ? (uint32_t)config->channels : 2U;
    return ORPHEUS_OK;
}

static int ns_reset(void* state) { (void)state; return ORPHEUS_OK; }

static int ns_process(void* state, const OrpheusProcessContext* ctx) {
    (void)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    /* 输入必须存在；无输出端口，无需处理 outputs */
    if (in == NULL) return ORPHEUS_ERR_INVALID_ARG;
    return ORPHEUS_OK;
}

static int ns_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    (void)state; (void)param_id; (void)value;
    return ORPHEUS_ERR_UNSUPPORTED;
}

static int ns_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    NullSinkState* s = (NullSinkState*)state;
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)s->channels;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int ns_register_slots(void* state, const OrpheusRegistry* reg) {
    NullSinkState* s = (NullSinkState*)state;
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_AFFECTS_SIGNATURE);
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
