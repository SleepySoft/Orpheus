#include "orpheus_sweep_gen.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t channels;
    double start_freq;
    double end_freq;
    double duration_s;
    double amplitude;
    bool log_scale;
    double t;     /* 已生成时长（秒） */
    double phase; /* 累积相位（弧度） */
} SweepGenState;

static const OrpheusParameter params[] = {
    {
        .id = "start_freq",
        .name = "Start Freq",
        .type = ORPHEUS_VALUE_FLOAT,
        .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 20.0f },
        .min_f32 = 1.0f,
        .max_f32 = 20000.0f,
        .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
        .persistent = true,
        .affects_signature = false
    },
    {
        .id = "end_freq",
        .name = "End Freq",
        .type = ORPHEUS_VALUE_FLOAT,
        .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 20000.0f },
        .min_f32 = 1.0f,
        .max_f32 = 20000.0f,
        .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
        .persistent = true,
        .affects_signature = false
    },
    {
        .id = "duration_s",
        .name = "Duration",
        .type = ORPHEUS_VALUE_FLOAT,
        .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 5.0f },
        .min_f32 = 0.1f,
        .max_f32 = 300.0f,
        .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
        .persistent = true,
        .affects_signature = false
    },
    {
        .id = "amplitude",
        .name = "Amplitude",
        .type = ORPHEUS_VALUE_FLOAT,
        .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.7f },
        .min_f32 = 0.0f,
        .max_f32 = 1.0f,
        .update_policy = ORPHEUS_UPDATE_SMOOTHED,
        .persistent = true,
        .affects_signature = false
    },
    {
        .id = "log_scale",
        .name = "Log Sweep",
        .type = ORPHEUS_VALUE_BOOL,
        .default_value = { .type = ORPHEUS_VALUE_BOOL, .value.b = true },
        .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
        .persistent = true,
        .affects_signature = false
    },
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
    }
};

static const OrpheusPort ports[] = {
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

static const OrpheusComponentDescriptor desc = {
    .id = "orpheus.builtin.sweep_gen",
    .version = "1.0.0",
    .abi_version = ORPHEUS_ABI_VERSION,
    .ports = ports,
    .port_count = 1,
    .params = params,
    .param_count = 6,
    .state_size = sizeof(SweepGenState),
    .scratch_size = 0,
    .alignment = 8,
    .latency_samples = 0,
    .realtime_safe = true,
    .supports_inplace = false
};

static const OrpheusComponentDescriptor* get_desc(void) {
    return &desc;
}

static int create(void** state, const OrpheusConfig* config) {
    (void)config;
    *state = calloc(1, sizeof(SweepGenState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}

static int destroy(void* state) {
    free(state);
    return ORPHEUS_OK;
}

static int prepare(void* state, const OrpheusConfig* config) {
    SweepGenState* s = (SweepGenState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    s->start_freq = 20.0;
    s->end_freq = 20000.0;
    s->duration_s = 5.0;
    s->amplitude = 0.7;
    s->log_scale = true;
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (!config->param_ids[i]) continue;
        const OrpheusValue* v = &config->param_values[i];
        if (strcmp(config->param_ids[i], "start_freq") == 0 && v->type == ORPHEUS_VALUE_FLOAT)
            s->start_freq = v->value.f32;
        else if (strcmp(config->param_ids[i], "end_freq") == 0 && v->type == ORPHEUS_VALUE_FLOAT)
            s->end_freq = v->value.f32;
        else if (strcmp(config->param_ids[i], "duration_s") == 0 && v->type == ORPHEUS_VALUE_FLOAT)
            s->duration_s = v->value.f32;
        else if (strcmp(config->param_ids[i], "amplitude") == 0 && v->type == ORPHEUS_VALUE_FLOAT)
            s->amplitude = v->value.f32;
        else if (strcmp(config->param_ids[i], "log_scale") == 0 && v->type == ORPHEUS_VALUE_BOOL)
            s->log_scale = v->value.b;
    }
    if (s->duration_s <= 0.0) s->duration_s = 1.0;
    s->t = 0.0;
    s->phase = 0.0;
    return ORPHEUS_OK;
}

static int reset(void* state) {
    SweepGenState* s = (SweepGenState*)state;
    s->t = 0.0;
    s->phase = 0.0;
    return ORPHEUS_OK;
}

static int process(void* state, const OrpheusProcessContext* ctx) {
    SweepGenState* s = (SweepGenState*)state;
    OrpheusBuffer* out = ctx->outputs[0];
    if (!out) return ORPHEUS_ERR_INVALID_ARG;

    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    float* out_data = (float*)out->data;
    double fs = ctx->sample_rate > 0 ? (double)ctx->sample_rate : 48000.0;
    double dt = 1.0 / fs;
    double dur = s->duration_s;
    double f0 = s->start_freq;
    double f1 = s->end_freq;
    double ratio = (f1 > 0.0 && f0 > 0.0) ? f1 / f0 : 1.0;

    for (uint32_t f = 0; f < frames; ++f) {
        double tt = s->t;
        double freq;
        if (tt >= dur) {
            freq = 0.0; /* 扫频结束后静音 */
        } else if (s->log_scale) {
            freq = f0 * pow(ratio, tt / dur);
        } else {
            freq = f0 + (f1 - f0) * (tt / dur);
        }
        float sample = freq > 0.0 ? (float)(s->amplitude * sin(s->phase)) : 0.0f;
        for (uint32_t c = 0; c < ch; ++c) {
            out_data[f * ch + c] = sample;
        }
        s->phase += 2.0 * 3.14159265358979323846 * freq * dt;
        if (s->phase > 6.28318530717958647692 * 64.0) s->phase -= 6.28318530717958647692 * 64.0;
        s->t += dt;
    }
    out->frame_count = frames;
    out->interleaved = true;
    return ORPHEUS_OK;
}

static int set_param(void* state, const char* id, const OrpheusValue* v) {
    SweepGenState* s = (SweepGenState*)state;
    if (strcmp(id, "amplitude") == 0 && v->type == ORPHEUS_VALUE_FLOAT) {
        s->amplitude = v->value.f32;
        return ORPHEUS_OK;
    }
    (void)s; (void)v;
    return ORPHEUS_ERR_UNSUPPORTED;
}

static int get_param(void* state, const char* id, OrpheusValue* v) {
    SweepGenState* s = (SweepGenState*)state;
    if (strcmp(id, "channels") == 0) {
        v->type = ORPHEUS_VALUE_INT;
        v->value.i32 = (int32_t)s->channels;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static const OrpheusComponentInterface iface = {
    .get_descriptor = get_desc,
    .create = create,
    .destroy = destroy,
    .prepare = prepare,
    .reset = reset,
    .process = process,
    .set_parameter = set_param,
    .get_parameter = get_param,
    .get_state_value = NULL
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {
    return &iface;
}
