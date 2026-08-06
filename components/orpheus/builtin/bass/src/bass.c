#include "orpheus_bass.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CHANNELS 32
#define PI_F 3.14159265358979f

/* Bass: 1st-order low shelf (parallel: dry + boost*LPF) with gain ramp, adapted from Bose BassDemo.
   out = in + boost * LPF(in); boost = 10^(gain_db/20) - 1 ramped; gain_db=0 is flat. */
typedef struct {
    float a1;               /* 1st-order LPF pole */
    float z[MAX_CHANNELS];  /* per-channel LPF state */
    float boost;            /* current boost amount (ramping) */
    float target_boost;     /* target boost amount */
    float smoothing_coeff;
    uint32_t channels;
} BassState;

static float read_float(const OrpheusConfig* config, const char* id, float fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_FLOAT) return config->param_values[i].value.f32;
            if (config->param_values[i].type == ORPHEUS_VALUE_INT) return (float)config->param_values[i].value.i32;
        }
    }
    return fallback;
}

static const OrpheusParameter bass_params[] = {
    { .id = "gain_db", .name = "Gain", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.0f },
      .min_f32 = -12.0f, .max_f32 = 12.0f, .unit = "dB", .update_policy = ORPHEUS_UPDATE_SMOOTHED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "fc", .name = "Cutoff", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 200.0f },
      .min_f32 = 50.0f, .max_f32 = 1000.0f, .unit = "Hz", .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
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

static const OrpheusPort bass_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor bass_descriptor = {
    .id = "orpheus.builtin.bass", .version = "1.0.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = bass_ports, .port_count = 2, .params = bass_params, .param_count = 4,
    .state_size = sizeof(BassState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = false
};

static const OrpheusComponentDescriptor* bass_get_descriptor(void) { return &bass_descriptor; }

static int bass_create(void** state, const OrpheusConfig* config) {
    (void)config;
    *state = calloc(1, sizeof(BassState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int bass_destroy(void* state) { free(state); return ORPHEUS_OK; }

static int bass_prepare(void* state, const OrpheusConfig* config) {
    BassState* s = (BassState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    float fc = read_float(config, "fc", 200.0f);
    float gain_db = read_float(config, "gain_db", 0.0f);
    float ramp_ms = read_float(config, "ramp_ms", 50.0f);
    if (config->sample_rate > 0 && fc > 0.0f) {
        s->a1 = expf(-2.0f * PI_F * fc / (float)config->sample_rate);
    } else {
        s->a1 = 0.0f;
    }
    s->target_boost = powf(10.0f, gain_db / 20.0f) - 1.0f;
    s->boost = s->target_boost;
    for (uint32_t c = 0; c < MAX_CHANNELS; ++c) s->z[c] = 0.0f;
    if (ramp_ms <= 0.0f || config->sample_rate == 0) {
        s->smoothing_coeff = 1.0f;
    } else {
        s->smoothing_coeff = 1.0f - expf(-1.0f / ((ramp_ms / 1000.0f) * (float)config->sample_rate));
        if (s->smoothing_coeff > 1.0f) s->smoothing_coeff = 1.0f;
    }
    return ORPHEUS_OK;
}
static int bass_reset(void* state) {
    BassState* s = (BassState*)state;
    for (uint32_t c = 0; c < MAX_CHANNELS; ++c) s->z[c] = 0.0f;
    return ORPHEUS_OK;
}
static int bass_process(void* state, const OrpheusProcessContext* ctx) {
    BassState* s = (BassState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (in == NULL || out == NULL) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;
    float b0 = 1.0f - s->a1;
    for (uint32_t n = 0; n < frames; ++n) {
        s->boost += s->smoothing_coeff * (s->target_boost - s->boost);
        for (uint32_t c = 0; c < ch; ++c) {
            float x = in_data[n * ch + c];
            float lpf = b0 * x + s->a1 * s->z[c];
            s->z[c] = lpf;
            out_data[n * ch + c] = x + s->boost * lpf;
        }
    }
    out->frame_count = frames;
    return ORPHEUS_OK;
}
static int bass_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    BassState* s = (BassState*)state;
    if (strcmp(param_id, "gain_db") == 0) {
        if (value->type != ORPHEUS_VALUE_FLOAT && value->type != ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        float db = (value->type == ORPHEUS_VALUE_FLOAT) ? value->value.f32 : (float)value->value.i32;
        s->target_boost = powf(10.0f, db / 20.0f) - 1.0f;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int bass_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    BassState* s = (BassState*)state;
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
static const OrpheusComponentInterface bass_interface = {
    .get_descriptor = bass_get_descriptor, .create = bass_create, .destroy = bass_destroy,
    .prepare = bass_prepare, .reset = bass_reset, .process = bass_process,
    .set_parameter = bass_set_parameter, .get_parameter = bass_get_parameter, .get_state_value = NULL
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &bass_interface; }