#include "orpheus_task_source.h"

#include <stdlib.h>
#include <string.h>

static const OrpheusParameter params[] = {
    {
        .id = "frequency_hz", .name = "任务频率", .type = ORPHEUS_VALUE_INT,
        .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 100 },
        .min_i32 = 1, .max_i32 = 192000, .unit = "Hz",
        .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
        .readback = true, .persistent = true
    }
};

static const OrpheusComponentDescriptor descriptor = {
    .id = "orpheus.builtin.task_source", .version = "1.0.0",
    .abi_version = ORPHEUS_ABI_VERSION,
    .params = params, .param_count = 1,
    .state_size = sizeof(TaskSourceState), .alignment = 8,
    .realtime_safe = true
};

static const OrpheusComponentDescriptor* get_descriptor(void) {
    return &descriptor;
}

static int create(void** state, const OrpheusConfig* config) {
    if (!state || !config) return ORPHEUS_ERR_INVALID_ARG;
    TaskSourceState* s = (TaskSourceState*)config->state_block;
    if (s) {
        s->owns_memory = false;
    } else {
        s = (TaskSourceState*)calloc(1, sizeof(*s));
        if (!s) return ORPHEUS_ERR_OUT_OF_MEMORY;
        s->owns_memory = true;
    }
    s->frequency_hz = 100;
    *state = s;
    return ORPHEUS_OK;
}

static int destroy(void* state) {
    if (state && ((TaskSourceState*)state)->owns_memory) free(state);
    return ORPHEUS_OK;
}

static int prepare(void* state, const OrpheusConfig* config) {
    if (!state || !config) return ORPHEUS_ERR_INVALID_ARG;
    int32_t frequency = 100;
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (strcmp(config->param_ids[i], "frequency_hz") != 0) continue;
        if (config->param_values[i].type != ORPHEUS_VALUE_INT)
            return ORPHEUS_ERR_INVALID_ARG;
        frequency = config->param_values[i].value.i32;
    }
    if (frequency < 1 || frequency > 192000) return ORPHEUS_ERR_INVALID_ARG;
    ((TaskSourceState*)state)->frequency_hz = frequency;
    return ORPHEUS_OK;
}

static int reset(void* state) {
    return state ? ORPHEUS_OK : ORPHEUS_ERR_INVALID_ARG;
}

static int process(void* state, const OrpheusProcessContext* ctx) {
    return state && ctx ? orpheus_process_next(ctx) : ORPHEUS_ERR_INVALID_ARG;
}

static int get_parameter(void* state, const char* id, OrpheusValue* value) {
    if (!state || !id || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(id, "frequency_hz") != 0) return ORPHEUS_ERR_NOT_FOUND;
    value->type = ORPHEUS_VALUE_INT;
    value->value.i32 = ((TaskSourceState*)state)->frequency_hz;
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface interface_ = {
    .get_descriptor = get_descriptor, .create = create, .destroy = destroy,
    .prepare = prepare, .reset = reset, .process = process,
    .get_parameter = get_parameter
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {
    return &interface_;
}
