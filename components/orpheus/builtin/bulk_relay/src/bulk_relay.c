#include "orpheus_bulk_relay.h"

#include <stdlib.h>
#include <string.h>

static int32_t read_int(const OrpheusConfig* config, const char* id, int32_t fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_INT) return config->param_values[i].value.i32;
            if (config->param_values[i].type == ORPHEUS_VALUE_FLOAT) return (int32_t)config->param_values[i].value.f32;
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

static const OrpheusParameter relay_params[] = {
    { .id = "count", .name = "有效元素数", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 4 },
      .min_i32 = 1, .max_i32 = ORPHEUS_BULK_RELAY_CAPACITY,
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true },
    { .id = "initial_values", .name = "初始数组", .type = ORPHEUS_VALUE_STRING,
      .default_value = { .type = ORPHEUS_VALUE_STRING, .value.str = "0,0,0,0" },
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = false, .persistent = true },
    { .id = "values", .name = "数组值", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.0f },
      .update_policy = ORPHEUS_UPDATE_BLOCK_BOUNDARY,
      .readback = true, .persistent = false }
};

static const OrpheusComponentDescriptor relay_descriptor = {
    .id = "orpheus.builtin.bulk_relay", .version = "1.0.0",
    .abi_version = ORPHEUS_ABI_VERSION,
    .ports = NULL, .port_count = 0,
    .params = relay_params, .param_count = 3,
    .state_size = sizeof(BulkRelayState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = false
};

static const OrpheusComponentDescriptor* relay_get_descriptor(void) { return &relay_descriptor; }

static int relay_create(void** state, const OrpheusConfig* config) {
    if (config && config->state_block) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(BulkRelayState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}

static int relay_destroy(void* state) { (void)state; return ORPHEUS_OK; }

static int relay_prepare(void* state, const OrpheusConfig* config) {
    BulkRelayState* relay = (BulkRelayState*)state;
    int32_t count = read_int(config, "count", 4);
    if (count < 1) count = 1;
    if (count > ORPHEUS_BULK_RELAY_CAPACITY) count = ORPHEUS_BULK_RELAY_CAPACITY;
    relay->count = (uint32_t)count;
    memset(relay->values, 0, sizeof(relay->values));
    const char* cursor = read_string(config, "initial_values", "0,0,0,0");
    for (uint32_t i = 0; i < relay->count && cursor && *cursor; ++i) {
        char* end = NULL;
        relay->values[i] = strtof(cursor, &end);
        if (end == cursor) break;
        cursor = end;
        while (*cursor == ',' || *cursor == ' ' || *cursor == '\t') ++cursor;
    }
    return ORPHEUS_OK;
}

static int relay_reset(void* state) { (void)state; return ORPHEUS_OK; }

static int relay_process(void* state, const OrpheusProcessContext* context) {
    (void)state;
    (void)context;
    return ORPHEUS_OK;
}

static int relay_get_parameter(void* state, const char* id, OrpheusValue* value) {
    BulkRelayState* relay = (BulkRelayState*)state;
    if (strcmp(id, "count") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)relay->count;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int relay_register_slots(void* state, const OrpheusRegistry* registry) {
    BulkRelayState* relay = (BulkRelayState*)state;
    ORPHEUS_REG_ARRAY(registry, relay, values, ORPHEUS_BULK_RELAY_CAPACITY,
                      ORPHEUS_SLOT_BULK, "values_storage", "数组存储",
                      ORPHEUS_VALUE_FLOAT, .flags=ORPHEUS_SLOT_DOUBLE_BUFFERED);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface relay_interface = {
    .get_descriptor = relay_get_descriptor,
    .create = relay_create, .destroy = relay_destroy,
    .prepare = relay_prepare, .reset = relay_reset, .process = relay_process,
    .set_parameter = NULL, .get_parameter = relay_get_parameter,
    .get_state_value = NULL, .register_slots = relay_register_slots,
    .hook = NULL
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &relay_interface; }