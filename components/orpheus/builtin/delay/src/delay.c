#include "orpheus_delay.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    float* buffer;
    uint32_t delay_samples;
    uint32_t write_pos;
    uint32_t channels;
    uint32_t capacity;
    float mix;
} DelayState;

static const OrpheusParameter params[] = {
    { .id = "delay_ms", .name = "Delay Time", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 100.0f },
      .min_f32 = 0.0f, .max_f32 = 5000.0f, .unit = "ms",
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED, .readback = true, .persistent = true },
    { .id = "mix", .name = "Mix", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.5f },
      .min_f32 = 0.0f, .max_f32 = 1.0f,
      .update_policy = ORPHEUS_UPDATE_SMOOTHED, .readback = true, .persistent = true },
    { .id = "channels", .name = "Channels", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true }
};

static const OrpheusPort ports[] = {
    { "in", ORPHEUS_PORT_INPUT, ORPHEUS_PORT_AUDIO, ORPHEUS_FORMAT_F32, 0, 0, 0, true, "channels" },
    { "out", ORPHEUS_PORT_OUTPUT, ORPHEUS_PORT_AUDIO, ORPHEUS_FORMAT_F32, 0, 0, 0, true, "channels" }
};

static const OrpheusComponentDescriptor desc = {
    "orpheus.builtin.delay", "1.0.0", ORPHEUS_ABI_VERSION,
    ports, 2, params, 3, sizeof(DelayState), 0, 8, 0, true, false
};

static const OrpheusComponentDescriptor* get_desc(void) { return &desc; }
static int create(void** state, const OrpheusConfig* config) {
    (void)config; *state = calloc(1, sizeof(DelayState)); return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int destroy(void* state) {
    DelayState* s = (DelayState*)state;
    if (s->buffer) free(s->buffer);
    free(s); return ORPHEUS_OK;
}
static int prepare(void* state, const OrpheusConfig* config) {
    DelayState* s = (DelayState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    float delay_ms = 100.0f;
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], "delay_ms") == 0 && config->param_values[i].type == ORPHEUS_VALUE_FLOAT)
            delay_ms = config->param_values[i].value.f32;
    }
    s->delay_samples = (uint32_t)((delay_ms / 1000.0f) * config->sample_rate + 0.5f);
    s->capacity = s->delay_samples * s->channels + 1024;
    s->buffer = (float*)calloc(s->capacity, sizeof(float));
    if (!s->buffer) return ORPHEUS_ERR_OUT_OF_MEMORY;
    s->write_pos = 0;
    s->mix = 0.5f;
    return ORPHEUS_OK;
}
static int reset(void* state) {
    DelayState* s = (DelayState*)state;
    if (s->buffer) memset(s->buffer, 0, s->capacity * sizeof(float));
    s->write_pos = 0; return ORPHEUS_OK;
}
static int process(void* state, const OrpheusProcessContext* ctx) {
    DelayState* s = (DelayState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (!in || !out) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;
    for (uint32_t n = 0; n < frames; ++n) {
        for (uint32_t c = 0; c < ch; ++c) {
            uint32_t idx = (s->write_pos + s->capacity - s->delay_samples * ch) % s->capacity;
            float delayed = s->buffer[idx + c];
            float x = in_data[n * ch + c];
            s->buffer[s->write_pos + c] = x;
            out_data[n * ch + c] = x + s->mix * delayed;
        }
        s->write_pos = (s->write_pos + ch) % s->capacity;
    }
    out->frame_count = frames;
    return ORPHEUS_OK;
}
static int set_param(void* state, const char* id, const OrpheusValue* v) {
    DelayState* s = (DelayState*)state;
    if (strcmp(id, "mix") == 0 && v->type == ORPHEUS_VALUE_FLOAT) { s->mix = v->value.f32; return ORPHEUS_OK; }
    return ORPHEUS_ERR_UNSUPPORTED;
}
static int get_param(void* state, const char* id, OrpheusValue* v) {
    DelayState* s = (DelayState*)state;
    if (strcmp(id, "mix") == 0) { v->type = ORPHEUS_VALUE_FLOAT; v->value.f32 = s->mix; return ORPHEUS_OK; }
    if (strcmp(id, "channels") == 0) { v->type = ORPHEUS_VALUE_INT; v->value.i32 = (int32_t)s->channels; return ORPHEUS_OK; }
    return ORPHEUS_ERR_NOT_FOUND;
}
static const OrpheusComponentInterface iface = {
    get_desc, create, destroy, prepare, reset, process, set_param, get_param, NULL
};
ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void) { return &iface; }
