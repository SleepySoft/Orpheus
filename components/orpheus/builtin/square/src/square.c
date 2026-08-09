#include "orpheus_square.h"

#include <stdlib.h>
#include <string.h>

static const OrpheusParameter square_params[] = {
    { .id = "channels", .name = "通道数", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true }
};

static const OrpheusPort square_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor square_descriptor = {
    .id = "orpheus.builtin.square", .version = "1.0.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = square_ports, .port_count = 2, .params = square_params, .param_count = 1,
    .state_size = sizeof(SquareState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = true
};

static const OrpheusComponentDescriptor* square_get_descriptor(void) { return &square_descriptor; }

static int square_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(SquareState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int square_destroy(void* state) { (void)state; return ORPHEUS_OK; }

static int square_prepare(void* state, const OrpheusConfig* config) {
    SquareState* s = (SquareState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    return ORPHEUS_OK;
}

static int square_reset(void* state) {
    SquareState* s = (SquareState*)state;
    s->channels = 2;
    return ORPHEUS_OK;
}

static int square_process(void* state, const OrpheusProcessContext* ctx) {
    SquareState* s = (SquareState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (in == NULL || out == NULL) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t n = ctx->frame_count * s->channels;
    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;
    for (uint32_t i = 0; i < n; ++i) out_data[i] = in_data[i] * in_data[i];
    out->frame_count = ctx->frame_count;
    return ORPHEUS_OK;
}

static int square_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    SquareState* s = (SquareState*)state;
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)s->channels;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int square_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    (void)state; (void)param_id; (void)value;
    return ORPHEUS_ERR_NOT_FOUND;
}

static int square_register_slots(void* state, const OrpheusRegistry* reg) {
    SquareState* s = (SquareState*)state;
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface square_interface = {
    .get_descriptor = square_get_descriptor, .create = square_create, .destroy = square_destroy,
    .prepare = square_prepare, .reset = square_reset, .process = square_process,
    .set_parameter = square_set_parameter, .get_parameter = square_get_parameter,
    .get_state_value = NULL, .register_slots = square_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {
    return &square_interface;
}
