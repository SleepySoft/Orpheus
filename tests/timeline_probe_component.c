#include "orpheus_abi.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    uint64_t frame_index;
    uint64_t epoch;
    uint32_t flags;
    uint32_t call_count;
    float timestamp;
    int owns_memory;
} TimelineProbeState;

static const OrpheusComponentDescriptor descriptor = {
    .id = "orpheus.test.timeline_probe",
    .version = "1.0.0",
    .abi_version = ORPHEUS_ABI_VERSION,
    .ports = NULL,
    .port_count = 0,
    .params = NULL,
    .param_count = 0,
    .state_size = sizeof(TimelineProbeState),
    .scratch_size = 0,
    .alignment = 8,
    .latency_samples = 0,
    .realtime_safe = true,
    .supports_inplace = false,
};

static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

static int create(void** state, const OrpheusConfig* config) {
    TimelineProbeState* probe;
    if (state == NULL || config == NULL) return ORPHEUS_ERR_INVALID_ARG;
    probe = config->state_block != NULL
        ? (TimelineProbeState*)config->state_block
        : (TimelineProbeState*)calloc(1, sizeof(TimelineProbeState));
    if (probe == NULL) return ORPHEUS_ERR_OUT_OF_MEMORY;
    memset(probe, 0, sizeof(*probe));
    probe->owns_memory = config->state_block == NULL;
    *state = probe;
    return ORPHEUS_OK;
}

static int destroy(void* state) {
    TimelineProbeState* probe = (TimelineProbeState*)state;
    if (probe != NULL && probe->owns_memory) free(probe);
    return ORPHEUS_OK;
}

static int prepare(void* state, const OrpheusConfig* config) {
    return state != NULL && config != NULL ? ORPHEUS_OK : ORPHEUS_ERR_INVALID_ARG;
}

static int reset(void* state) {
    TimelineProbeState* probe = (TimelineProbeState*)state;
    int owns_memory;
    if (probe == NULL) return ORPHEUS_ERR_INVALID_ARG;
    owns_memory = probe->owns_memory;
    memset(probe, 0, sizeof(*probe));
    probe->owns_memory = owns_memory;
    return ORPHEUS_OK;
}

static int process(void* state, const OrpheusProcessContext* context) {
    TimelineProbeState* probe = (TimelineProbeState*)state;
    if (probe == NULL || context == NULL) return ORPHEUS_ERR_INVALID_ARG;
    probe->call_count++;
    probe->frame_index = context->frame_index;
    probe->epoch = context->epoch;
    probe->flags = context->timeline_flags;
    probe->timestamp = (float)context->timestamp;
    return ORPHEUS_OK;
}

static int get_parameter(void* state, const char* id, OrpheusValue* value) {
    TimelineProbeState* probe = (TimelineProbeState*)state;
    if (probe == NULL || id == NULL || value == NULL) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(id, "frame_index") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)probe->frame_index;
        return ORPHEUS_OK;
    }
    if (strcmp(id, "epoch") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)probe->epoch;
        return ORPHEUS_OK;
    }
    if (strcmp(id, "flags") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)probe->flags;
        return ORPHEUS_OK;
    }
    if (strcmp(id, "call_count") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)probe->call_count;
        return ORPHEUS_OK;
    }
    if (strcmp(id, "timestamp") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT;
        value->value.f32 = probe->timestamp;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static const OrpheusComponentInterface interface = {
    get_descriptor,
    create,
    destroy,
    prepare,
    reset,
    process,
    NULL,
    get_parameter,
    NULL,
    NULL,
    NULL,
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif

ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {
    return &interface;
}
