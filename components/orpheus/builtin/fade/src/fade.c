#include "orpheus_fade.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CHANNELS 32
#define PI_F 3.14159265358979f

/* Fade: spectral front/back fade (LPF always full, HPF scaled by front/back gain ramp), adapted from Bose FadeDemo.
   out = LPF(in) + gain * (in - LPF(in)); front group uses front_gain, back group uses back_gain.
   fade: -1=full back, +1=full front, 0=no attenuation. */
typedef struct {
    float a1;                 /* 1st-order LPF pole (crossover) */
    float z[MAX_CHANNELS];    /* per-channel LPF state */
    float front_gain;         /* current front gain (ramping) */
    float back_gain;          /* current back gain (ramping) */
    float target_front;
    float target_back;
    float fade;               /* param cache (readback) */
    float smoothing_coeff;
    uint32_t channels;
    uint32_t front_channels;  /* 0..front_channels-1 are front */
} FadeState;

static float read_float(const OrpheusConfig* config, const char* id, float fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_FLOAT) return config->param_values[i].value.f32;
            if (config->param_values[i].type == ORPHEUS_VALUE_INT) return (float)config->param_values[i].value.i32;
        }
    }
    return fallback;
}

static void fade_targets(float fade, float* tf, float* tb) {
    if (fade >= 0.0f) { *tf = 1.0f; *tb = 1.0f - fade; }
    else              { *tf = 1.0f + fade; *tb = 1.0f; }
}

static const OrpheusParameter fade_params[] = {
    { .id = "fade", .name = "Fade", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.0f },
      .min_f32 = -1.0f, .max_f32 = 1.0f, .update_policy = ORPHEUS_UPDATE_SMOOTHED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "crossover_hz", .name = "Crossover", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 400.0f },
      .min_f32 = 100.0f, .max_f32 = 2000.0f, .unit = "Hz", .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "channels", .name = "Channels", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 4 },
      .min_i32 = 2, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "front_channels", .name = "Front Channels", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 31, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "ramp_ms", .name = "Ramp Time", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 50.0f },
      .min_f32 = 0.0f, .max_f32 = 1000.0f, .unit = "ms", .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = false }
};

static const OrpheusPort fade_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor fade_descriptor = {
    .id = "orpheus.builtin.fade", .version = "1.0.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = fade_ports, .port_count = 2, .params = fade_params, .param_count = 5,
    .state_size = sizeof(FadeState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = false
};

static const OrpheusComponentDescriptor* fade_get_descriptor(void) { return &fade_descriptor; }

static int fade_create(void** state, const OrpheusConfig* config) {
    (void)config;
    *state = calloc(1, sizeof(FadeState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int fade_destroy(void* state) { free(state); return ORPHEUS_OK; }

static int fade_prepare(void* state, const OrpheusConfig* config) {
    FadeState* s = (FadeState*)state;
    s->channels = config->channels > 0 ? config->channels : 4;
    float fc = read_float(config, "crossover_hz", 400.0f);
    s->fade = read_float(config, "fade", 0.0f);
    float fch = read_float(config, "front_channels", 2.0f);
    s->front_channels = (uint32_t)fch;
    if (s->front_channels < 1) s->front_channels = 1;
    if (s->front_channels >= s->channels) s->front_channels = s->channels - 1;
    float ramp_ms = read_float(config, "ramp_ms", 50.0f);
    if (config->sample_rate > 0 && fc > 0.0f) {
        s->a1 = expf(-2.0f * PI_F * fc / (float)config->sample_rate);
    } else {
        s->a1 = 0.0f;
    }
    fade_targets(s->fade, &s->target_front, &s->target_back);
    s->front_gain = s->target_front;
    s->back_gain = s->target_back;
    for (uint32_t c = 0; c < MAX_CHANNELS; ++c) s->z[c] = 0.0f;
    if (ramp_ms <= 0.0f || config->sample_rate == 0) {
        s->smoothing_coeff = 1.0f;
    } else {
        s->smoothing_coeff = 1.0f - expf(-1.0f / ((ramp_ms / 1000.0f) * (float)config->sample_rate));
        if (s->smoothing_coeff > 1.0f) s->smoothing_coeff = 1.0f;
    }
    return ORPHEUS_OK;
}
static int fade_reset(void* state) {
    FadeState* s = (FadeState*)state;
    for (uint32_t c = 0; c < MAX_CHANNELS; ++c) s->z[c] = 0.0f;
    return ORPHEUS_OK;
}
static int fade_process(void* state, const OrpheusProcessContext* ctx) {
    FadeState* s = (FadeState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (in == NULL || out == NULL) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;
    float b0 = 1.0f - s->a1;
    for (uint32_t n = 0; n < frames; ++n) {
        s->front_gain += s->smoothing_coeff * (s->target_front - s->front_gain);
        s->back_gain += s->smoothing_coeff * (s->target_back - s->back_gain);
        for (uint32_t c = 0; c < ch; ++c) {
            float x = in_data[n * ch + c];
            float lpf = b0 * x + s->a1 * s->z[c];
            s->z[c] = lpf;
            float g = (c < s->front_channels) ? s->front_gain : s->back_gain;
            out_data[n * ch + c] = lpf + g * (x - lpf);
        }
    }
    out->frame_count = frames;
    return ORPHEUS_OK;
}
static int fade_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    FadeState* s = (FadeState*)state;
    if (strcmp(param_id, "fade") == 0) {
        if (value->type != ORPHEUS_VALUE_FLOAT && value->type != ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        s->fade = (value->type == ORPHEUS_VALUE_FLOAT) ? value->value.f32 : (float)value->value.i32;
        if (s->fade < -1.0f) s->fade = -1.0f;
        if (s->fade > 1.0f) s->fade = 1.0f;
        fade_targets(s->fade, &s->target_front, &s->target_back);
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int fade_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    FadeState* s = (FadeState*)state;
    if (strcmp(param_id, "fade") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT; value->value.f32 = s->fade; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->channels; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "front_channels") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->front_channels; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static const OrpheusComponentInterface fade_interface = {
    .get_descriptor = fade_get_descriptor, .create = fade_create, .destroy = fade_destroy,
    .prepare = fade_prepare, .reset = fade_reset, .process = fade_process,
    .set_parameter = fade_set_parameter, .get_parameter = fade_get_parameter, .get_state_value = NULL
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &fade_interface; }