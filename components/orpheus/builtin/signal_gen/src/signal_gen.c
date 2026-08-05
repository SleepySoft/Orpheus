#include "orpheus_signal_gen.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    float phase;
    float frequency;
    float amplitude;
    uint32_t channels;
} SignalGenState;

static const OrpheusParameter params[] = {
    { .id = "frequency", .name = "Frequency", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 440.0f },
      .min_f32 = 1.0f, .max_f32 = 20000.0f, .unit = "Hz",
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED, .readback = true, .persistent = true },
    { .id = "amplitude", .name = "Amplitude", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.5f },
      .min_f32 = 0.0f, .max_f32 = 1.0f,
      .update_policy = ORPHEUS_UPDATE_SMOOTHED, .readback = true, .persistent = true },
    { .id = "channels", .name = "Channels", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true }
};

static const OrpheusPort ports[] = {
    { "out", ORPHEUS_PORT_OUTPUT, ORPHEUS_PORT_AUDIO, ORPHEUS_FORMAT_F32, 0, 0, 0, true, "channels" }
};

static const OrpheusComponentDescriptor desc = {
    "orpheus.builtin.signal_gen", "1.0.0", ORPHEUS_ABI_VERSION,
    ports, 1, params, 3, sizeof(SignalGenState), 0, 8, 0, true, false
};

static const OrpheusComponentDescriptor* get_desc(void) { return &desc; }
static int create(void** state, const OrpheusConfig* config) {
    (void)config; *state = calloc(1, sizeof(SignalGenState)); return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int destroy(void* state) { free(state); return ORPHEUS_OK; }
static int prepare(void* state, const OrpheusConfig* config) {
    SignalGenState* s = (SignalGenState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    s->frequency = 440.0f; s->amplitude = 0.5f;
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (!config->param_ids[i]) continue;
        if (strcmp(config->param_ids[i], "frequency") == 0 && config->param_values[i].type == ORPHEUS_VALUE_FLOAT)
            s->frequency = config->param_values[i].value.f32;
        if (strcmp(config->param_ids[i], "amplitude") == 0 && config->param_values[i].type == ORPHEUS_VALUE_FLOAT)
            s->amplitude = config->param_values[i].value.f32;
    }
    s->phase = 0.0f;
    return ORPHEUS_OK;
}
static int reset(void* state) { SignalGenState* s = (SignalGenState*)state; s->phase = 0.0f; return ORPHEUS_OK; }
static int process(void* state, const OrpheusProcessContext* ctx) {
    SignalGenState* s = (SignalGenState*)state;
    OrpheusBuffer* out = ctx->outputs[0];
    if (!out) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    float* out_data = (float*)out->data;
    float step = 2.0f * 3.14159265358979f * s->frequency / ctx->sample_rate;
    for (uint32_t n = 0; n < frames; ++n) {
        float sample = s->amplitude * sinf(s->phase);
        s->phase += step;
        if (s->phase > 2.0f * 3.14159265358979f) s->phase -= 2.0f * 3.14159265358979f;
        for (uint32_t c = 0; c < ch; ++c) out_data[n * ch + c] = sample;
    }
    out->frame_count = frames;
    return ORPHEUS_OK;
}
static int set_param(void* state, const char* id, const OrpheusValue* v) {
    SignalGenState* s = (SignalGenState*)state;
    if (strcmp(id, "amplitude") == 0 && v->type == ORPHEUS_VALUE_FLOAT) { s->amplitude = v->value.f32; return ORPHEUS_OK; }
    return ORPHEUS_ERR_UNSUPPORTED;
}
static int get_param(void* state, const char* id, OrpheusValue* v) {
    SignalGenState* s = (SignalGenState*)state;
    if (strcmp(id, "amplitude") == 0) { v->type = ORPHEUS_VALUE_FLOAT; v->value.f32 = s->amplitude; return ORPHEUS_OK; }
    if (strcmp(id, "frequency") == 0) { v->type = ORPHEUS_VALUE_FLOAT; v->value.f32 = s->frequency; return ORPHEUS_OK; }
    if (strcmp(id, "channels") == 0) { v->type = ORPHEUS_VALUE_INT; v->value.i32 = (int32_t)s->channels; return ORPHEUS_OK; }
    return ORPHEUS_ERR_NOT_FOUND;
}
static const OrpheusComponentInterface iface = {
    get_desc, create, destroy, prepare, reset, process, set_param, get_param, NULL
};
ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void) { return &iface; }
