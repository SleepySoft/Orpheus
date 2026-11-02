#include "data_probe.h"

#include <string.h>

static const OrpheusParameter parameters[] = {
    {.id="mode", .name="运算", .type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=0},
     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED},
    {.id="fallback", .name="缺省加数", .type=ORPHEUS_VALUE_FLOAT,
     .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .readback=true, .persistent=true}
};

static const OrpheusPort ports[] = {
    {.id="audio", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO,
     .sample_format=ORPHEUS_FORMAT_F32, .channels=1},
    {.id="vector", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_BULK,
     .sample_format=ORPHEUS_FORMAT_F32, .channels=2, .block_size=129},
    {.id="scalar", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_CONTROL,
     .sample_format=ORPHEUS_FORMAT_F32, .channels=1, .block_size=1},
    {.id="held", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_CONTROL,
     .sample_format=ORPHEUS_FORMAT_F32, .channels=1, .block_size=1},
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO,
     .sample_format=ORPHEUS_FORMAT_F32, .channels=1},
    {.id="vector_out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_BULK,
     .sample_format=ORPHEUS_FORMAT_F32, .channels=2, .block_size=129},
    {.id="scalar_out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_CONTROL,
     .sample_format=ORPHEUS_FORMAT_F32, .channels=1, .block_size=1}
};

static const OrpheusComponentDescriptor descriptor = {
    .id = "orpheus.test.data_probe",
    .version = "1.0.0",
    .abi_version = ORPHEUS_ABI_VERSION,
    .ports = ports,
    .port_count = sizeof(ports) / sizeof(ports[0]),
    .params = parameters,
    .param_count = sizeof(parameters) / sizeof(parameters[0]),
    .state_size = sizeof(DataProbeState),
    .realtime_safe = true
};

static const OrpheusComponentDescriptor* get_descriptor(void) {
    return &descriptor;
}

static int create(void** output, const OrpheusConfig* config) {
    DataProbeState* state = (DataProbeState*)config->state_block;
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    memset(state, 0, sizeof(*state));
    *output = state;
    return ORPHEUS_OK;
}

static int destroy(void* state) {
    (void)state;
    return ORPHEUS_OK;
}

static int prepare(void* state, const OrpheusConfig* config) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (strcmp(config->param_ids[i], "mode") == 0) {
            if (config->param_values[i].type != ORPHEUS_VALUE_INT)
                return ORPHEUS_ERR_INVALID_ARG;
            ((DataProbeState*)state)->mode = config->param_values[i].value.i32;
        } else if (strcmp(config->param_ids[i], "fallback") == 0) {
            if (config->param_values[i].type != ORPHEUS_VALUE_FLOAT)
                return ORPHEUS_ERR_INVALID_ARG;
            ((DataProbeState*)state)->fallback = config->param_values[i].value.f32;
        }
    }
    return ORPHEUS_OK;
}

static int reset(void* state) {
    memset(((DataProbeState*)state)->metrics, 0, sizeof(((DataProbeState*)state)->metrics));
    return ORPHEUS_OK;
}

static int process(void* opaque, const OrpheusProcessContext* ctx) {
    DataProbeState* state = (DataProbeState*)opaque;
    const uint32_t call = (uint32_t)state->metrics[0] + 1;
    float result = 0.0f;
    if (state->mode == 0) {
        OrpheusBuffer* vector = ctx->outputs[1];
        OrpheusBuffer* scalar = ctx->outputs[2];
        if (vector) {
            float* values = (float*)vector->data;
            for (uint32_t i = 0; i < vector->frame_count * vector->channels; ++i)
                values[i] = (float)(call + i);
        }
        if (scalar) ((float*)scalar->data)[0] = (float)(10 * call);
    } else {
        const OrpheusBuffer* vector = ctx->inputs[1];
        const OrpheusBuffer* scalar = ctx->inputs[2] ? ctx->inputs[2] : ctx->inputs[3];
        if (vector) {
            const float* values = (const float*)vector->data;
            for (uint32_t i = 0; i < vector->frame_count * vector->channels; ++i)
                result += values[i];
            state->metrics[4] = (float)vector->frame_count;
            state->metrics[5] = (float)vector->channels;
        }
        if (scalar) {
            result += ((const float*)scalar->data)[0];
            state->metrics[6] = (float)scalar->frame_count;
        } else {
            result += state->fallback;
        }
    }
    if (ctx->outputs[0]) {
        OrpheusBuffer* audio = ctx->outputs[0];
        float* output = (float*)audio->data;
        for (uint32_t i = 0; i < audio->frame_count * audio->channels; ++i)
            output[i] = result / 65536.0f;
    }
    state->metrics[0] = (float)call;
    state->metrics[1] = (float)ctx->frame_count;
    state->metrics[2] = (float)ctx->frame_index;
    state->metrics[3] = (float)ctx->valid_frames;
    state->metrics[7] = state->fallback;
    if (call <= 24) state->metrics[7 + call] = result;
    return ORPHEUS_OK;
}

static int set_parameter(void* opaque, const char* key, const OrpheusValue* value) {
    if (strcmp(key, "fallback") == 0) {
        if (value->type != ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        ((DataProbeState*)opaque)->fallback = value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "mode") != 0) return ORPHEUS_ERR_NOT_FOUND;
    if (value->type != ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
    ((DataProbeState*)opaque)->mode = value->value.i32;
    return ORPHEUS_OK;
}

static int get_parameter(void* opaque, const char* key, OrpheusValue* value) {
    if (strcmp(key, "fallback") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT;
        value->value.f32 = ((DataProbeState*)opaque)->fallback;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "mode") != 0) return ORPHEUS_ERR_NOT_FOUND;
    value->type = ORPHEUS_VALUE_INT;
    value->value.i32 = ((DataProbeState*)opaque)->mode;
    return ORPHEUS_OK;
}

static int register_slots(void* opaque, const OrpheusRegistry* registry) {
    DataProbeState* state = (DataProbeState*)opaque;
    ORPHEUS_REG_SLOT(registry, state, fallback, ORPHEUS_SLOT_SETTING, "fallback", "缺省加数",
                    ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_IMMEDIATE,
                    .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT |
                           ORPHEUS_SLOT_DIRECT_WRITE);
    ORPHEUS_REG_ARRAY(registry, state, metrics, 32, ORPHEUS_SLOT_BULK, "metrics", "验收记录",
                     ORPHEUS_VALUE_FLOAT, .flags=ORPHEUS_SLOT_READBACK);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface interface = {
    .get_descriptor = get_descriptor,
    .create = create,
    .destroy = destroy,
    .prepare = prepare,
    .reset = reset,
    .process = process,
    .set_parameter = set_parameter,
    .get_parameter = get_parameter,
    .register_slots = register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {
    return &interface;
}
