#include "orpheus_probe_rms.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static const OrpheusParameter probe_rms_params[] = {
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
    },
    {
        .id = "rms",
        .name = "RMS",
        .type = ORPHEUS_VALUE_FLOAT,
        .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.0f },
        .update_policy = ORPHEUS_UPDATE_IMMEDIATE,
        .readback = true,
        .persistent = false,
        .affects_signature = false
    }
};

static const OrpheusPort probe_rms_ports[] = {
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
    },
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

static const OrpheusComponentDescriptor probe_rms_descriptor = {
    .id = "orpheus.builtin.probe_rms",
    .version = "1.0.0",
    .abi_version = ORPHEUS_ABI_VERSION,
    .ports = probe_rms_ports,
    .port_count = 2,
    .params = probe_rms_params,
    .param_count = 2,
    .state_size = sizeof(ProbeRmsState),
    .scratch_size = 0,
    .alignment = 8,
    .latency_samples = 0,
    .realtime_safe = true,
    .supports_inplace = true
};

static const OrpheusComponentDescriptor* probe_rms_get_descriptor(void) {
    return &probe_rms_descriptor;
}

static int probe_rms_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(ProbeRmsState));
    if (*state == NULL) return ORPHEUS_ERR_OUT_OF_MEMORY;
    return ORPHEUS_OK;
}

static int probe_rms_destroy(void* state) {
    (void)state; /* v2：状态内存由 Runtime 统一管理 */
    return ORPHEUS_OK;
}

static int probe_rms_prepare(void* state, const OrpheusConfig* config) {
    ProbeRmsState* s = (ProbeRmsState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    s->rms = 0.0f;
    return ORPHEUS_OK;
}

static int probe_rms_reset(void* state) {
    ProbeRmsState* s = (ProbeRmsState*)state;
    s->rms = 0.0f;
    return ORPHEUS_OK;
}

static int probe_rms_process(void* state, const OrpheusProcessContext* ctx) {
    ProbeRmsState* s = (ProbeRmsState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];

    if (in == NULL || out == NULL) return ORPHEUS_ERR_INVALID_ARG;

    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;

    double sum = 0.0;
    for (uint32_t i = 0; i < frames * ch; ++i) {
        out_data[i] = in_data[i];
        sum += (double)in_data[i] * (double)in_data[i];
    }

    s->rms = (float)sqrt(sum / (frames * ch));
    out->frame_count = frames;
    return ORPHEUS_OK;
}

static int probe_rms_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    (void)state; (void)param_id; (void)value;
    return ORPHEUS_ERR_UNSUPPORTED;
}

static int probe_rms_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    ProbeRmsState* s = (ProbeRmsState*)state;
    if (strcmp(param_id, "rms") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT;
        value->value.f32 = s->rms;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)s->channels;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int probe_rms_register_slots(void* state, const OrpheusRegistry* reg) {
    ProbeRmsState* s = (ProbeRmsState*)state;
    ORPHEUS_REG_SLOT(reg, s, rms, ORPHEUS_SLOT_PROBE, "rms", "RMS",
                     ORPHEUS_VALUE_FLOAT, .flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface probe_rms_interface = {
    .get_descriptor = probe_rms_get_descriptor,
    .create = probe_rms_create,
    .destroy = probe_rms_destroy,
    .prepare = probe_rms_prepare,
    .reset = probe_rms_reset,
    .process = probe_rms_process,
    .set_parameter = probe_rms_set_parameter,
    .get_parameter = probe_rms_get_parameter,
    .get_state_value = NULL,
    .register_slots = probe_rms_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {
    return &probe_rms_interface;
}
