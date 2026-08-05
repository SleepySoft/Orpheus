#include "orpheus_device_out.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t channels;
} DeviceOutState;

static const OrpheusParameter device_out_params[] = {
    {
        .id = "channels",
        .name = "Channels",
        .type = ORPHEUS_VALUE_INT,
        .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
        .min_i32 = 1,
        .max_i32 = 32,
        .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
        .readback = true,
        .persistent = true,
        .affects_signature = true
    }
};

static const OrpheusPort device_out_ports[] = {
    {
        .id = "in",
        .direction = ORPHEUS_PORT_INPUT,
        .type = ORPHEUS_PORT_AUDIO,
        .sample_format = ORPHEUS_FORMAT_F32,
        .channels = 0,
        .sample_rate = 0,
        .block_size = 0,
        .is_variable = true,
        .channels_param = "channels"
    }
};

static const OrpheusComponentDescriptor device_out_descriptor = {
    .id = "orpheus.builtin.device_out",
    .version = "1.0.0",
    .abi_version = ORPHEUS_ABI_VERSION,
    .ports = device_out_ports,
    .port_count = 1,
    .params = device_out_params,
    .param_count = 1,
    .state_size = sizeof(DeviceOutState),
    .scratch_size = 0,
    .alignment = 8,
    .latency_samples = 0,
    .realtime_safe = true,
    .supports_inplace = false
};

static const OrpheusComponentDescriptor* device_out_get_descriptor(void) {
    return &device_out_descriptor;
}

static int device_out_create(void** state, const OrpheusConfig* config) {
    (void)config;
    *state = calloc(1, sizeof(DeviceOutState));
    if (*state == NULL) return ORPHEUS_ERR_OUT_OF_MEMORY;
    return ORPHEUS_OK;
}

static int device_out_destroy(void* state) {
    free(state);
    return ORPHEUS_OK;
}

static int device_out_prepare(void* state, const OrpheusConfig* config) {
    DeviceOutState* s = (DeviceOutState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    return ORPHEUS_OK;
}

static int device_out_reset(void* state) {
    (void)state;
    return ORPHEUS_OK;
}

static int device_out_process(void* state, const OrpheusProcessContext* ctx) {
    DeviceOutState* s = (DeviceOutState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    (void)in;
    (void)s;
    /* Host reads the input buffer after process; no work here. */
    return ORPHEUS_OK;
}

static int device_out_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    (void)state; (void)param_id; (void)value;
    return ORPHEUS_ERR_UNSUPPORTED;
}

static int device_out_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    DeviceOutState* s = (DeviceOutState*)state;
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)s->channels;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static const OrpheusComponentInterface device_out_interface = {
    .get_descriptor = device_out_get_descriptor,
    .create = device_out_create,
    .destroy = device_out_destroy,
    .prepare = device_out_prepare,
    .reset = device_out_reset,
    .process = device_out_process,
    .set_parameter = device_out_set_parameter,
    .get_parameter = device_out_get_parameter,
    .get_state_value = NULL
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {
    return &device_out_interface;
}
