#include "orpheus_merge.h"

#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t channels; } MergeState;

static const OrpheusParameter params[] = {
    { .id = "channels", .name = "Channels", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true }
};

static const OrpheusPort ports[] = {
    { "in0", ORPHEUS_PORT_INPUT, ORPHEUS_PORT_AUDIO, ORPHEUS_FORMAT_F32, 0, 0, 0, true, "channels" },
    { "in1", ORPHEUS_PORT_INPUT, ORPHEUS_PORT_AUDIO, ORPHEUS_FORMAT_F32, 0, 0, 0, true, "channels" },
    { "out", ORPHEUS_PORT_OUTPUT, ORPHEUS_PORT_AUDIO, ORPHEUS_FORMAT_F32, 0, 0, 0, true, "channels" }
};

static const OrpheusComponentDescriptor desc = {
    "orpheus.builtin.merge", "1.0.0", ORPHEUS_ABI_VERSION,
    ports, 3, params, 1, sizeof(MergeState), 0, 8, 0, true, false
};

static const OrpheusComponentDescriptor* get_desc(void) { return &desc; }
static int create(void** state, const OrpheusConfig* config) {
    (void)config; *state = calloc(1, sizeof(MergeState)); return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int destroy(void* state) { free(state); return ORPHEUS_OK; }
static int prepare(void* state, const OrpheusConfig* config) {
    MergeState* s = (MergeState*)state; s->channels = config->channels > 0 ? config->channels : 2; return ORPHEUS_OK;
}
static int reset(void* state) { (void)state; return ORPHEUS_OK; }
static int process(void* state, const OrpheusProcessContext* ctx) {
    MergeState* s = (MergeState*)state;
    const OrpheusBuffer* in0 = ctx->inputs[0];
    const OrpheusBuffer* in1 = ctx->inputs[1];
    OrpheusBuffer* out = ctx->outputs[0];
    if (!in0 || !in1 || !out) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t n = ctx->frame_count * s->channels;
    const float* a = (const float*)in0->data;
    const float* b = (const float*)in1->data;
    float* c = (float*)out->data;
    for (uint32_t i = 0; i < n; ++i) c[i] = a[i] + b[i];
    out->frame_count = ctx->frame_count;
    return ORPHEUS_OK;
}
static int set_param(void* state, const char* id, const OrpheusValue* v) { (void)state; (void)id; (void)v; return ORPHEUS_ERR_UNSUPPORTED; }
static int get_param(void* state, const char* id, OrpheusValue* v) {
    MergeState* s = (MergeState*)state;
    if (strcmp(id, "channels") == 0) { v->type = ORPHEUS_VALUE_INT; v->value.i32 = (int32_t)s->channels; return ORPHEUS_OK; }
    return ORPHEUS_ERR_NOT_FOUND;
}
static const OrpheusComponentInterface iface = {
    get_desc, create, destroy, prepare, reset, process, set_param, get_param, NULL
};
ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void) { return &iface; }
