#include "orpheus_resample.h"

#include <stdlib.h>
#include <string.h>

#define RESAMPLE_MAX_CHANNELS 32

/* Integer decimation N:1 with a moving-average anti-alias filter.
 * Runs every block at the input rate; emits exactly one output block of
 * ctx->frame_count samples every N input blocks (downstream fires then). */
typedef struct {
    uint32_t factor;
    uint32_t channels;
    float acc[RESAMPLE_MAX_CHANNELS];
    uint32_t n;          /* input samples accumulated in current group */
    uint32_t write_pos;  /* frames written into the output block */
} ResampleState;

static const OrpheusParameter params[] = {
    { .id = "factor", .name = "Factor", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 2, .max_i32 = 64, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
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
    "orpheus.builtin.resample", "1.0.0", ORPHEUS_ABI_VERSION,
    ports, 2, params, 2, sizeof(ResampleState), 0, 8, 0, true, false
};

static const OrpheusComponentDescriptor* get_desc(void) { return &desc; }
static int create(void** state, const OrpheusConfig* config) {
    (void)config; *state = calloc(1, sizeof(ResampleState)); return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int destroy(void* state) { free(state); return ORPHEUS_OK; }
static int prepare(void* state, const OrpheusConfig* config) {
    ResampleState* s = (ResampleState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    if (s->channels > RESAMPLE_MAX_CHANNELS) s->channels = RESAMPLE_MAX_CHANNELS;
    s->factor = 2;
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], "factor") == 0 &&
            config->param_values[i].type == ORPHEUS_VALUE_INT) {
            s->factor = (uint32_t)config->param_values[i].value.i32;
        }
    }
    if (s->factor < 2) s->factor = 2;
    return ORPHEUS_OK;
}
static int reset(void* state) {
    ResampleState* s = (ResampleState*)state;
    memset(s->acc, 0, sizeof(s->acc));
    s->n = 0;
    s->write_pos = 0;
    return ORPHEUS_OK;
}

static int process(void* state, const OrpheusProcessContext* ctx) {
    ResampleState* s = (ResampleState*)state;
    if (ctx->input_count < 1 || !ctx->inputs[0]) return ORPHEUS_ERR_INVALID_ARG;
    if (ctx->output_count < 1 || !ctx->outputs[0]) return ORPHEUS_ERR_INVALID_ARG;
    const float* in = (const float*)ctx->inputs[0]->data;
    OrpheusBuffer* out = ctx->outputs[0];
    float* dst = (float*)out->data;
    uint32_t ch = s->channels;
    uint32_t n = s->factor;

    for (uint32_t f = 0; f < ctx->frame_count; ++f) {
        for (uint32_t c = 0; c < ch; ++c) {
            s->acc[c] += in[(size_t)f * ch + c];
        }
        if (++s->n >= n) {
            for (uint32_t c = 0; c < ch; ++c) {
                dst[(size_t)s->write_pos * ch + c] = s->acc[c] / (float)n;
                s->acc[c] = 0.0f;
            }
            s->n = 0;
            if (++s->write_pos >= out->frame_capacity) {
                s->write_pos = 0;
                out->frame_count = out->frame_capacity;
            }
        }
    }
    return ORPHEUS_OK;
}

static int set_param(void* state, const char* id, const OrpheusValue* v) { (void)state; (void)id; (void)v; return ORPHEUS_ERR_UNSUPPORTED; }
static int get_param(void* state, const char* id, OrpheusValue* v) {
    ResampleState* s = (ResampleState*)state;
    if (strcmp(id, "factor") == 0) { v->type = ORPHEUS_VALUE_INT; v->value.i32 = (int32_t)s->factor; return ORPHEUS_OK; }
    if (strcmp(id, "channels") == 0) { v->type = ORPHEUS_VALUE_INT; v->value.i32 = (int32_t)s->channels; return ORPHEUS_OK; }
    return ORPHEUS_ERR_NOT_FOUND;
}
static const OrpheusComponentInterface iface = {
    get_desc, create, destroy, prepare, reset, process, set_param, get_param, NULL
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &iface; }
