#include "orpheus_soft_clipper.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static float read_float(const OrpheusConfig* config, const char* id, float fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_FLOAT) return config->param_values[i].value.f32;
            if (config->param_values[i].type == ORPHEUS_VALUE_INT) return (float)config->param_values[i].value.i32;
        }
    }
    return fallback;
}

static float db_to_linear(float db) { return powf(10.0f, db / 20.0f); }

static const OrpheusParameter sc_params[] = {
    { .id = "drive_db", .name = "驱动", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.0f },
      .min_f32 = -12.0f, .max_f32 = 24.0f, .unit = "dB",
      .update_policy = ORPHEUS_UPDATE_SMOOTHED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "channels", .name = "通道数", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true }
};

static const OrpheusPort sc_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor sc_descriptor = {
    .id = "orpheus.builtin.soft_clipper", .version = "1.0.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = sc_ports, .port_count = 2, .params = sc_params, .param_count = 2,
    .state_size = sizeof(SoftClipperState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = true
};

static const OrpheusComponentDescriptor* sc_get_descriptor(void) { return &sc_descriptor; }

static int sc_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(SoftClipperState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int sc_destroy(void* state) { (void)state; return ORPHEUS_OK; }

static int sc_prepare(void* state, const OrpheusConfig* config) {
    SoftClipperState* s = (SoftClipperState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    s->drive_db = read_float(config, "drive_db", 0.0f);
    s->drive_linear = db_to_linear(s->drive_db);
    s->norm = (s->drive_linear > 0.0001f) ? 1.0f / tanhf(s->drive_linear) : 1.0f;
    return ORPHEUS_OK;
}

static int sc_reset(void* state) {
    SoftClipperState* s = (SoftClipperState*)state;
    s->drive_db = 0.0f;
    s->drive_linear = 1.0f;
    s->norm = 1.0f / tanhf(1.0f);
    return ORPHEUS_OK;
}

static int sc_process(void* state, const OrpheusProcessContext* ctx) {
    SoftClipperState* s = (SoftClipperState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (in == NULL || out == NULL) return ORPHEUS_ERR_INVALID_ARG;

    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;
    float drive = db_to_linear(s->drive_db);
    float norm = (drive > 0.0001f) ? 1.0f / tanhf(drive) : 1.0f;
    s->drive_linear = drive;
    s->norm = norm;
    for (uint32_t i = 0; i < frames * ch; ++i) {
        float x = in_data[i] * drive;
        out_data[i] = tanhf(x) * norm;
    }
    out->frame_count = frames;
    return ORPHEUS_OK;
}

static int sc_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    SoftClipperState* s = (SoftClipperState*)state;
    if (strcmp(param_id, "drive_db") == 0) {
        if (value->type != ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        s->drive_db = value->value.f32;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int sc_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    SoftClipperState* s = (SoftClipperState*)state;
    if (strcmp(param_id, "drive_db") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT;
        value->value.f32 = s->drive_db;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)s->channels;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int sc_register_slots(void* state, const OrpheusRegistry* reg) {
    SoftClipperState* s = (SoftClipperState*)state;
    ORPHEUS_REG_SLOT(reg, s, drive_db, ORPHEUS_SLOT_SETTING, "drive_db", "驱动",
                     ORPHEUS_VALUE_FLOAT, .min_f32=-12.0f, .max_f32=24.0f, .unit="dB",
                     .update_policy=ORPHEUS_UPDATE_SMOOTHED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_DIRECT_WRITE);
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface sc_interface = {
    .get_descriptor = sc_get_descriptor, .create = sc_create, .destroy = sc_destroy,
    .prepare = sc_prepare, .reset = sc_reset, .process = sc_process,
    .set_parameter = sc_set_parameter, .get_parameter = sc_get_parameter,
    .get_state_value = NULL, .register_slots = sc_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {
    return &sc_interface;
}
