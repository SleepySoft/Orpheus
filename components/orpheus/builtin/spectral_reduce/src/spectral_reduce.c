#include "orpheus_spectral_reduce.h"

#include <float.h>
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

static const char* read_string(const OrpheusConfig* config, const char* id, const char* fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0 &&
            config->param_values[i].type == ORPHEUS_VALUE_STRING) {
            return config->param_values[i].value.str ? config->param_values[i].value.str : fallback;
        }
    }
    return fallback;
}

static const OrpheusParameter sr_params[] = {
    { .id = "channels", .name = "通道数", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 64, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "fft_size", .name = "FFT 点数", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 128 },
      .min_i32 = 2, .max_i32 = 4096, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "num_frames", .name = "每块帧数", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 12 },
      .min_i32 = 1, .max_i32 = 256, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "bin_count", .name = "参与运算的 bin 数", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 0 },
      .min_i32 = 0, .max_i32 = 4096, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "operation", .name = "运算", .type = ORPHEUS_VALUE_STRING,
      .default_value = { .type = ORPHEUS_VALUE_STRING, .value.str = "mean" },
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = false }
};

static const OrpheusPort sr_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor sr_descriptor = {
    .id = "orpheus.builtin.spectral_reduce", .version = "1.0.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = sr_ports, .port_count = 2, .params = sr_params, .param_count = 5,
    .state_size = sizeof(SpectralReduceState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = false
};

static const OrpheusComponentDescriptor* sr_get_descriptor(void) { return &sr_descriptor; }

static int sr_create(void** state, const OrpheusConfig* config) {
    (void)config;
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(SpectralReduceState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}

static int sr_destroy(void* state) { (void)state; return ORPHEUS_OK; }

static int sr_prepare(void* state, const OrpheusConfig* config) {
    SpectralReduceState* s = (SpectralReduceState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    s->fft_size = (uint32_t)read_float(config, "fft_size", 128.0f);
    s->num_frames = (uint32_t)read_float(config, "num_frames", 12.0f);
    s->bin_count = (uint32_t)read_float(config, "bin_count", 0.0f);
    if (s->fft_size < 2) s->fft_size = 128;
    if (s->num_frames < 1) s->num_frames = 12;
    if (s->bin_count == 0 || s->bin_count > s->fft_size) s->bin_count = s->fft_size / 2 + 1;

    const char* op_str = read_string(config, "operation", "mean");
    s->operation = 1;
    if (op_str && strcmp(op_str, "sum") == 0) s->operation = 0;
    else if (op_str && strcmp(op_str, "min") == 0) s->operation = 2;
    else if (op_str && strcmp(op_str, "max") == 0) s->operation = 3;
    return ORPHEUS_OK;
}

static int sr_reset(void* state) { (void)state; return ORPHEUS_OK; }

static int sr_process(void* state, const OrpheusProcessContext* ctx) {
    SpectralReduceState* s = (SpectralReduceState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (in == NULL || out == NULL) return ORPHEUS_ERR_INVALID_ARG;

    uint32_t ch = s->channels;
    uint32_t frame_size = s->fft_size;
    uint32_t nframes = s->num_frames;
    uint32_t bin_count = s->bin_count;
    uint32_t op = s->operation;
    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;
    uint32_t frames = ctx->frame_count;

    for (uint32_t c = 0; c < ch; ++c) {
        float acc = 0.0f;
        float vmin = FLT_MAX;
        float vmax = -FLT_MAX;
        uint32_t count = 0;
        for (uint32_t f = 0; f < nframes; ++f) {
            for (uint32_t k = 0; k < bin_count; ++k) {
                uint32_t idx = (f * frame_size + k) * ch + c;
                if (idx >= frames * ch) continue;
                float v = in_data[idx];
                acc += v;
                if (v < vmin) vmin = v;
                if (v > vmax) vmax = v;
                ++count;
            }
        }
        float result = 0.0f;
        if (count > 0) {
            if (op == 0) result = acc;
            else if (op == 1) result = acc / (float)count;
            else if (op == 2) result = vmin;
            else result = vmax;
        }
        for (uint32_t f = 0; f < frames; ++f) {
            out_data[f * ch + c] = result;
        }
    }

    out->frame_count = frames;
    return ORPHEUS_OK;
}

static int sr_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    SpectralReduceState* s = (SpectralReduceState*)state;
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->channels; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "fft_size") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->fft_size; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "num_frames") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->num_frames; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "bin_count") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->bin_count; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "operation") == 0) {
        value->type = ORPHEUS_VALUE_STRING;
        value->value.str = s->operation == 0 ? "sum" : (s->operation == 2 ? "min" : (s->operation == 3 ? "max" : "mean"));
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int sr_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    (void)state; (void)param_id; (void)value;
    return ORPHEUS_ERR_UNSUPPORTED;
}

static int sr_register_slots(void* state, const OrpheusRegistry* reg) {
    SpectralReduceState* s = (SpectralReduceState*)state;
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=64,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, fft_size, ORPHEUS_SLOT_SETTING, "fft_size", "FFT 点数",
                     ORPHEUS_VALUE_INT, .min_i32=2, .max_i32=4096,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, num_frames, ORPHEUS_SLOT_SETTING, "num_frames", "每块帧数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=256,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, bin_count, ORPHEUS_SLOT_SETTING, "bin_count", "参与运算的 bin 数",
                     ORPHEUS_VALUE_INT, .min_i32=0, .max_i32=4096,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, operation, ORPHEUS_SLOT_SETTING, "operation", "运算",
                     ORPHEUS_VALUE_STRING,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface sr_interface = {
    .get_descriptor = sr_get_descriptor, .create = sr_create, .destroy = sr_destroy,
    .prepare = sr_prepare, .reset = sr_reset, .process = sr_process,
    .set_parameter = sr_set_parameter, .get_parameter = sr_get_parameter,
    .get_state_value = NULL, .register_slots = sr_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &sr_interface; }
