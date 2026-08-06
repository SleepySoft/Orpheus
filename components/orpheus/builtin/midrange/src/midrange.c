#include "orpheus_midrange.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CHANNELS 32
#define PI_F 3.14159265358979f

/* MidRange: 2nd-order bandpass shelf (Direct Form II Transposed) with gain ramp, adapted from Bose MidRangeDemo.
   out = in + boost * BPF(in); b2 = -b0 (bandpass); boost = 10^(gain_db/20)-1 ramped. */
typedef struct {
    float b0, a1, a2;            /* 2nd-order bandpass coeffs (b2 = -b0) */
    float z1[MAX_CHANNELS];      /* per-channel DF2T state 1 */
    float z2[MAX_CHANNELS];      /* per-channel DF2T state 2 */
    float boost;
    float target_boost;
    float smoothing_coeff;
    uint32_t channels;
} MidRangeState;

static float read_float(const OrpheusConfig* config, const char* id, float fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_FLOAT) return config->param_values[i].value.f32;
            if (config->param_values[i].type == ORPHEUS_VALUE_INT) return (float)config->param_values[i].value.i32;
        }
    }
    return fallback;
}

static const OrpheusParameter mr_params[] = {
    { .id = "gain_db", .name = "Gain", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.0f },
      .min_f32 = -12.0f, .max_f32 = 12.0f, .unit = "dB", .update_policy = ORPHEUS_UPDATE_SMOOTHED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "fc", .name = "Center", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 1000.0f },
      .min_f32 = 200.0f, .max_f32 = 8000.0f, .unit = "Hz", .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "q", .name = "Q", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 1.0f },
      .min_f32 = 0.1f, .max_f32 = 10.0f, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "channels", .name = "Channels", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "ramp_ms", .name = "Ramp Time", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 50.0f },
      .min_f32 = 0.0f, .max_f32 = 1000.0f, .unit = "ms", .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = false }
};

static const OrpheusPort mr_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor mr_descriptor = {
    .id = "orpheus.builtin.midrange", .version = "1.0.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = mr_ports, .port_count = 2, .params = mr_params, .param_count = 5,
    .state_size = sizeof(MidRangeState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = false
};

static const OrpheusComponentDescriptor* mr_get_descriptor(void) { return &mr_descriptor; }

static int mr_create(void** state, const OrpheusConfig* config) {
    (void)config;
    *state = calloc(1, sizeof(MidRangeState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int mr_destroy(void* state) { free(state); return ORPHEUS_OK; }

static int mr_prepare(void* state, const OrpheusConfig* config) {
    MidRangeState* s = (MidRangeState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    float fc = read_float(config, "fc", 1000.0f);
    float q = read_float(config, "q", 1.0f);
    float gain_db = read_float(config, "gain_db", 0.0f);
    float ramp_ms = read_float(config, "ramp_ms", 50.0f);
    if (config->sample_rate > 0 && fc > 0.0f && q > 0.0f) {
        float w0 = 2.0f * PI_F * fc / (float)config->sample_rate;
        float alpha = sinf(w0) / (2.0f * q);
        float a0 = 1.0f + alpha;
        s->b0 = alpha / a0;            /* b2 = -b0 */
        s->a1 = -2.0f * cosf(w0) / a0;
        s->a2 = (1.0f - alpha) / a0;
    } else {
        s->b0 = s->a1 = s->a2 = 0.0f;
    }
    s->target_boost = powf(10.0f, gain_db / 20.0f) - 1.0f;
    s->boost = s->target_boost;
    for (uint32_t c = 0; c < MAX_CHANNELS; ++c) { s->z1[c] = 0.0f; s->z2[c] = 0.0f; }
    if (ramp_ms <= 0.0f || config->sample_rate == 0) {
        s->smoothing_coeff = 1.0f;
    } else {
        s->smoothing_coeff = 1.0f - expf(-1.0f / ((ramp_ms / 1000.0f) * (float)config->sample_rate));
        if (s->smoothing_coeff > 1.0f) s->smoothing_coeff = 1.0f;
    }
    return ORPHEUS_OK;
}
static int mr_reset(void* state) {
    MidRangeState* s = (MidRangeState*)state;
    for (uint32_t c = 0; c < MAX_CHANNELS; ++c) { s->z1[c] = 0.0f; s->z2[c] = 0.0f; }
    return ORPHEUS_OK;
}
static int mr_process(void* state, const OrpheusProcessContext* ctx) {
    MidRangeState* s = (MidRangeState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (in == NULL || out == NULL) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;
    float b0 = s->b0, a1 = s->a1, a2 = s->a2;
    for (uint32_t n = 0; n < frames; ++n) {
        s->boost += s->smoothing_coeff * (s->target_boost - s->boost);
        for (uint32_t c = 0; c < ch; ++c) {
            float x = in_data[n * ch + c];
            float filt = b0 * x + s->z1[c];
            s->z1[c] = s->z2[c] - a1 * filt;
            s->z2[c] = (-b0) * x - a2 * filt;
            out_data[n * ch + c] = x + s->boost * filt;
        }
    }
    out->frame_count = frames;
    return ORPHEUS_OK;
}
static int mr_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    MidRangeState* s = (MidRangeState*)state;
    if (strcmp(param_id, "gain_db") == 0) {
        if (value->type != ORPHEUS_VALUE_FLOAT && value->type != ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        float db = (value->type == ORPHEUS_VALUE_FLOAT) ? value->value.f32 : (float)value->value.i32;
        s->target_boost = powf(10.0f, db / 20.0f) - 1.0f;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int mr_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    MidRangeState* s = (MidRangeState*)state;
    if (strcmp(param_id, "gain_db") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT;
        value->value.f32 = 20.0f * log10f(s->boost + 1.0f);
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->channels; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static const OrpheusComponentInterface mr_interface = {
    .get_descriptor = mr_get_descriptor, .create = mr_create, .destroy = mr_destroy,
    .prepare = mr_prepare, .reset = mr_reset, .process = mr_process,
    .set_parameter = mr_set_parameter, .get_parameter = mr_get_parameter, .get_state_value = NULL
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &mr_interface; }