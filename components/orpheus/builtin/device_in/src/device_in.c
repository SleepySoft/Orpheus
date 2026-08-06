#include "orpheus_device_in.h"

#include <stdlib.h>
#include <string.h>

static const OrpheusParameter device_in_params[] = {
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

static const OrpheusPort device_in_ports[] = {
    {
        .id = "out",
        .direction = ORPHEUS_PORT_OUTPUT,
        .type = ORPHEUS_PORT_AUDIO,
        .sample_format = ORPHEUS_FORMAT_F32,
        .channels = 0,
        .sample_rate = 0,
        .block_size = 0,
        .is_variable = true,
        .channels_param = "channels"
    }
};

static const OrpheusComponentDescriptor device_in_descriptor = {
    .id = "orpheus.builtin.device_in",
    .version = "1.0.0",
    .abi_version = ORPHEUS_ABI_VERSION,
    .ports = device_in_ports,
    .port_count = 1,
    .params = device_in_params,
    .param_count = 1,
    .state_size = sizeof(DeviceInState),
    .scratch_size = 0,
    .alignment = 8,
    .latency_samples = 0,
    .realtime_safe = true,
    .supports_inplace = false
};

static const OrpheusComponentDescriptor* device_in_get_descriptor(void) {
    return &device_in_descriptor;
}

static int device_in_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(DeviceInState));
    if (*state == NULL) return ORPHEUS_ERR_OUT_OF_MEMORY;
    return ORPHEUS_OK;
}

static int device_in_destroy(void* state) {
    (void)state; /* v2：内存由 Runtime 统一管理 */
    return ORPHEUS_OK;
}

static int device_in_prepare(void* state, const OrpheusConfig* config) {
    DeviceInState* s = (DeviceInState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    return ORPHEUS_OK;
}

static int device_in_reset(void* state) {
    (void)state;
    return ORPHEUS_OK;
}

static int device_in_process(void* state, const OrpheusProcessContext* ctx) {
    DeviceInState* s = (DeviceInState*)state;
    OrpheusBuffer* out = ctx->outputs[0];
    out->frame_count = ctx->frame_count;
    out->channels = s->channels;
    out->interleaved = true;
    /* Host fills the buffer before calling process; no work here. */
    return ORPHEUS_OK;
}

static int device_in_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    (void)state; (void)param_id; (void)value;
    return ORPHEUS_ERR_UNSUPPORTED;
}

static int device_in_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    DeviceInState* s = (DeviceInState*)state;
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)s->channels;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int device_in_register_slots(void* state, const OrpheusRegistry* reg) {
    DeviceInState* s = (DeviceInState*)state;
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface device_in_interface = {
    .get_descriptor = device_in_get_descriptor,
    .create = device_in_create,
    .destroy = device_in_destroy,
    .prepare = device_in_prepare,
    .reset = device_in_reset,
    .process = device_in_process,
    .set_parameter = device_in_set_parameter,
    .get_parameter = device_in_get_parameter,
    .get_state_value = NULL,
    .register_slots = device_in_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {
    return &device_in_interface;
}
