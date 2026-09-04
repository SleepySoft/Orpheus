#include "orpheus_async_bridge.h"

#include <string.h>

static int32_t read_i32(const OrpheusConfig* config, const char* id, int32_t fallback) {
    uint32_t i;
    for (i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] == NULL || strcmp(config->param_ids[i], id) != 0) continue;
        if (config->param_values[i].type == ORPHEUS_VALUE_INT) {
            return config->param_values[i].value.i32;
        }
    }
    return fallback;
}

static const OrpheusParameter bridge_params[] = {
    { .id = "channels", .name = "\u901a\u9053\u6570", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 1 },
      .min_i32 = 1, .max_i32 = 32,
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "capacity_frames", .name = "\u7f13\u51b2\u5bb9\u91cf", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 0 },
      .min_i32 = 0, .max_i32 = 1048576,
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true },
    { .id = "level_frames", .name = "\u5f53\u524d\u6c34\u4f4d", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 0 },
      .update_policy = ORPHEUS_UPDATE_IMMEDIATE, .readback = true },
    { .id = "underruns", .name = "\u6b20\u8f7d\u6b21\u6570", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 0 },
      .update_policy = ORPHEUS_UPDATE_IMMEDIATE, .readback = true },
    { .id = "overruns", .name = "\u6ea2\u51fa\u6b21\u6570", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 0 },
      .update_policy = ORPHEUS_UPDATE_IMMEDIATE, .readback = true }
};

static const OrpheusPort bridge_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0,
      .block_size = 0, .is_variable = true, .channels_param = "channels" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0,
      .block_size = 0, .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor bridge_desc = {
    .id = "orpheus.builtin.async_bridge", .version = "1.0.0",
    .abi_version = ORPHEUS_ABI_VERSION,
    .ports = bridge_ports, .port_count = 2,
    .params = bridge_params, .param_count = 5,
    .state_size = sizeof(AsyncBridgeState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = false
};

static const OrpheusComponentDescriptor* bridge_get_descriptor(void) { return &bridge_desc; }

static int bridge_create(void** state, const OrpheusConfig* config) {
    if (state == NULL || config == NULL || config->state_block == NULL) return ORPHEUS_ERR_INVALID_ARG;
    *state = config->state_block;
    memset(*state, 0, sizeof(AsyncBridgeState));
    return ORPHEUS_OK;
}

static int bridge_destroy(void* state) { (void)state; return ORPHEUS_OK; }

static int bridge_prepare(void* state, const OrpheusConfig* config) {
    AsyncBridgeState* bridge = (AsyncBridgeState*)state;
    bridge->channels = config->channels > 0 ? config->channels : 1;
    bridge->capacity_frames = (uint32_t)read_i32(config, "capacity_frames", 0);
    bridge->level_frames = 0;
    bridge->underruns = 0;
    bridge->overruns = 0;
    return ORPHEUS_OK;
}

static int bridge_reset(void* state) {
    AsyncBridgeState* bridge = (AsyncBridgeState*)state;
    bridge->level_frames = 0;
    bridge->underruns = 0;
    bridge->overruns = 0;
    return ORPHEUS_OK;
}

static int bridge_process(void* state, const OrpheusProcessContext* ctx) {
    AsyncBridgeState* bridge = (AsyncBridgeState*)state;
    const OrpheusBuffer* input;
    OrpheusBuffer* output;
    uint32_t frames;
    size_t samples;
    if (ctx == NULL || ctx->input_count < 1 || ctx->output_count < 1) return ORPHEUS_ERR_INVALID_ARG;
    input = ctx->inputs[0];
    output = ctx->outputs[0];
    if (input == NULL || output == NULL || output->data == NULL) return ORPHEUS_ERR_INVALID_ARG;
    frames = input->frame_count < output->frame_capacity ? input->frame_count : output->frame_capacity;
    samples = (size_t)frames * bridge->channels;
    if (input->data != NULL && samples > 0) memcpy(output->data, input->data, samples * sizeof(float));
    if (frames < output->frame_capacity) {
        memset((float*)output->data + samples, 0,
               (size_t)(output->frame_capacity - frames) * bridge->channels * sizeof(float));
    }
    output->frame_count = output->frame_capacity;
    return ORPHEUS_OK;
}

static int bridge_set_parameter(void* state, const char* id, const OrpheusValue* value) {
    AsyncBridgeState* bridge = (AsyncBridgeState*)state;
    if (value == NULL || value->type != ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(id, "level_frames") == 0) bridge->level_frames = value->value.i32;
    else if (strcmp(id, "underruns") == 0) bridge->underruns = value->value.i32;
    else if (strcmp(id, "overruns") == 0) bridge->overruns = value->value.i32;
    else return ORPHEUS_ERR_UNSUPPORTED;
    return ORPHEUS_OK;
}

static int bridge_get_parameter(void* state, const char* id, OrpheusValue* value) {
    AsyncBridgeState* bridge = (AsyncBridgeState*)state;
    if (value == NULL) return ORPHEUS_ERR_INVALID_ARG;
    value->type = ORPHEUS_VALUE_INT;
    if (strcmp(id, "channels") == 0) value->value.i32 = (int32_t)bridge->channels;
    else if (strcmp(id, "capacity_frames") == 0) value->value.i32 = (int32_t)bridge->capacity_frames;
    else if (strcmp(id, "level_frames") == 0) value->value.i32 = bridge->level_frames;
    else if (strcmp(id, "underruns") == 0) value->value.i32 = bridge->underruns;
    else if (strcmp(id, "overruns") == 0) value->value.i32 = bridge->overruns;
    else return ORPHEUS_ERR_NOT_FOUND;
    return ORPHEUS_OK;
}

static int bridge_register_slots(void* state, const OrpheusRegistry* registry) {
    AsyncBridgeState* bridge = (AsyncBridgeState*)state;
    ORPHEUS_REG_SLOT(registry, bridge, channels, ORPHEUS_SLOT_SETTING, "channels",
                     "\u901a\u9053\u6570", ORPHEUS_VALUE_INT,
                     .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(registry, bridge, capacity_frames, ORPHEUS_SLOT_SETTING, "capacity_frames",
                     "\u7f13\u51b2\u5bb9\u91cf", ORPHEUS_VALUE_INT,
                     .min_i32=0, .max_i32=1048576,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(registry, bridge, level_frames, ORPHEUS_SLOT_PROBE, "level_frames",
                     "\u5f53\u524d\u6c34\u4f4d", ORPHEUS_VALUE_INT, .flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(registry, bridge, underruns, ORPHEUS_SLOT_PROBE, "underruns",
                     "\u6b20\u8f7d\u6b21\u6570", ORPHEUS_VALUE_INT, .flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(registry, bridge, overruns, ORPHEUS_SLOT_PROBE, "overruns",
                     "\u6ea2\u51fa\u6b21\u6570", ORPHEUS_VALUE_INT, .flags=ORPHEUS_SLOT_READBACK);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface bridge_iface = {
    bridge_get_descriptor, bridge_create, bridge_destroy, bridge_prepare, bridge_reset,
    bridge_process, bridge_set_parameter, bridge_get_parameter, NULL, bridge_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &bridge_iface; }
