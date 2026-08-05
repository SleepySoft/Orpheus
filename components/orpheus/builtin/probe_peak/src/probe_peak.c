#include "orpheus_probe_peak.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct { float peak; uint32_t channels; } ProbePeakState;

static const OrpheusParameter params[] = {
    { .id = "channels", .name = "Channels", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "peak", .name = "Peak", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.0f },
      .update_policy = ORPHEUS_UPDATE_IMMEDIATE, .readback = true, .persistent = false }
};

static const OrpheusPort ports[] = {
    { "in", ORPHEUS_PORT_INPUT, ORPHEUS_PORT_AUDIO, ORPHEUS_FORMAT_F32, 0, 0, 0, true, "channels" },
    { "out", ORPHEUS_PORT_OUTPUT, ORPHEUS_PORT_AUDIO, ORPHEUS_FORMAT_F32, 0, 0, 0, true, "channels" }
};

static const OrpheusComponentDescriptor desc = {
    "orpheus.builtin.probe_peak", "1.0.0", ORPHEUS_ABI_VERSION,
    ports, 2, params, 2, sizeof(ProbePeakState), 0, 8, 0, true, true
};

static const OrpheusComponentDescriptor* get_desc(void) { return &desc; }
static int create(void** state, const OrpheusConfig* config) {
    (void)config; *state = calloc(1, sizeof(ProbePeakState)); return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int destroy(void* state) { free(state); return ORPHEUS_OK; }
static int prepare(void* state, const OrpheusConfig* config) {
    ProbePeakState* s = (ProbePeakState*)state; s->channels = config->channels > 0 ? config->channels : 2; s->peak = 0.0f; return ORPHEUS_OK;
}
static int reset(void* state) { ProbePeakState* s = (ProbePeakState*)state; s->peak = 0.0f; return ORPHEUS_OK; }
static int process(void* state, const OrpheusProcessContext* ctx) {
    ProbePeakState* s = (ProbePeakState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (!in || !out) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t n = ctx->frame_count * s->channels;
    const float* src = (const float*)in->data;
    float* dst = (float*)out->data;
    float peak = 0.0f;
    for (uint32_t i = 0; i < n; ++i) {
        dst[i] = src[i];
        float a = fabsf(src[i]);
        if (a > peak) peak = a;
    }
    s->peak = peak;
    out->frame_count = ctx->frame_count;
    return ORPHEUS_OK;
}
static int set_param(void* state, const char* id, const OrpheusValue* v) { (void)state; (void)id; (void)v; return ORPHEUS_ERR_UNSUPPORTED; }
static int get_param(void* state, const char* id, OrpheusValue* v) {
    ProbePeakState* s = (ProbePeakState*)state;
    if (strcmp(id, "peak") == 0) { v->type = ORPHEUS_VALUE_FLOAT; v->value.f32 = s->peak; return ORPHEUS_OK; }
    if (strcmp(id, "channels") == 0) { v->type = ORPHEUS_VALUE_INT; v->value.i32 = (int32_t)s->channels; return ORPHEUS_OK; }
    return ORPHEUS_ERR_NOT_FOUND;
}
static const OrpheusComponentInterface iface = {
    get_desc, create, destroy, prepare, reset, process, set_param, get_param, NULL
};
ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void) { return &iface; }
