#include "orpheus_deinterleave.h"

#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t channels; } DeinterleaveState;

static const OrpheusParameter params[] = {
    { .id = "channels", .name = "Channels", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true }
};

/* 'out' is a variable-count port: expanded to out0..outN-1 at compile time. */
static const OrpheusPort ports[] = {
    { "in", ORPHEUS_PORT_INPUT, ORPHEUS_PORT_AUDIO, ORPHEUS_FORMAT_F32, 0, 0, 0, true, "channels" },
    { "out", ORPHEUS_PORT_OUTPUT, ORPHEUS_PORT_AUDIO, ORPHEUS_FORMAT_F32, 1, 0, 0, true, "channels" }
};

static const OrpheusComponentDescriptor desc = {
    "orpheus.builtin.deinterleave", "1.0.0", ORPHEUS_ABI_VERSION,
    ports, 2, params, 1, sizeof(DeinterleaveState), 0, 8, 0, true, false
};

static const OrpheusComponentDescriptor* get_desc(void) { return &desc; }
static int create(void** state, const OrpheusConfig* config) {
    (void)config; *state = calloc(1, sizeof(DeinterleaveState)); return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int destroy(void* state) { free(state); return ORPHEUS_OK; }
static int prepare(void* state, const OrpheusConfig* config) {
    DeinterleaveState* s = (DeinterleaveState*)state; s->channels = config->channels > 0 ? config->channels : 2; return ORPHEUS_OK;
}
static int reset(void* state) { (void)state; return ORPHEUS_OK; }

/* in: one interleaved N-channel buffer -> outputs: N mono buffers (unconnected pins skipped) */
static int process(void* state, const OrpheusProcessContext* ctx) {
    DeinterleaveState* s = (DeinterleaveState*)state;
    uint32_t nch = s->channels;
    if (nch == 0 || ctx->input_count < 1 || !ctx->inputs[0]) return ORPHEUS_ERR_INVALID_ARG;
    const float* src = (const float*)ctx->inputs[0]->data;
    for (uint32_t ch = 0; ch < ctx->output_count; ch++) {
        OrpheusBuffer* out = ctx->outputs[ch];
        if (!out) continue; /* unconnected pin */
        float* dst = (float*)out->data;
        uint32_t sch = ch < nch ? ch : nch - 1;
        for (uint32_t f = 0; f < ctx->frame_count; f++) {
            dst[f] = src[(size_t)f * nch + sch];
        }
        out->frame_count = ctx->frame_count;
    }
    return ORPHEUS_OK;
}

static int set_param(void* state, const char* id, const OrpheusValue* v) { (void)state; (void)id; (void)v; return ORPHEUS_ERR_UNSUPPORTED; }
static int get_param(void* state, const char* id, OrpheusValue* v) {
    DeinterleaveState* s = (DeinterleaveState*)state;
    if (strcmp(id, "channels") == 0) { v->type = ORPHEUS_VALUE_INT; v->value.i32 = (int32_t)s->channels; return ORPHEUS_OK; }
    return ORPHEUS_ERR_NOT_FOUND;
}
static const OrpheusComponentInterface iface = {
    get_desc, create, destroy, prepare, reset, process, set_param, get_param, NULL
};
ORPHEUS_API const OrpheusComponentInterface* orpheus_get_interface(void) { return &iface; }
