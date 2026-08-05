#include "orpheus_interleave.h"

#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t channels; } InterleaveState;

static const OrpheusParameter params[] = {
    { .id = "channels", .name = "Channels", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true }
};

/* 'in' is a variable-count port: expanded to in0..inN-1 at compile time. */
static const OrpheusPort ports[] = {
    { "in", ORPHEUS_PORT_INPUT, ORPHEUS_PORT_AUDIO, ORPHEUS_FORMAT_F32, 1, 0, 0, true, "channels" },
    { "out", ORPHEUS_PORT_OUTPUT, ORPHEUS_PORT_AUDIO, ORPHEUS_FORMAT_F32, 0, 0, 0, true, "channels" }
};

static const OrpheusComponentDescriptor desc = {
    "orpheus.builtin.interleave", "1.0.0", ORPHEUS_ABI_VERSION,
    ports, 2, params, 1, sizeof(InterleaveState), 0, 8, 0, true, false
};

static const OrpheusComponentDescriptor* get_desc(void) { return &desc; }
static int create(void** state, const OrpheusConfig* config) {
    (void)config; *state = calloc(1, sizeof(InterleaveState)); return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int destroy(void* state) { free(state); return ORPHEUS_OK; }
static int prepare(void* state, const OrpheusConfig* config) {
    InterleaveState* s = (InterleaveState*)state; s->channels = config->channels > 0 ? config->channels : 2; return ORPHEUS_OK;
}
static int reset(void* state) { (void)state; return ORPHEUS_OK; }

/* inputs: N mono buffers (unconnected pins -> silence) -> out: one interleaved N-channel buffer */
static int process(void* state, const OrpheusProcessContext* ctx) {
    InterleaveState* s = (InterleaveState*)state;
    uint32_t nch = s->channels;
    if (nch == 0 || ctx->output_count < 1 || !ctx->outputs[0]) return ORPHEUS_ERR_INVALID_ARG;
    OrpheusBuffer* out = ctx->outputs[0];
    float* dst = (float*)out->data;
    for (uint32_t ch = 0; ch < nch; ch++) {
        const OrpheusBuffer* inb = (ch < ctx->input_count) ? ctx->inputs[ch] : NULL;
        const float* src = inb ? (const float*)inb->data : NULL;
        if (src) {
            for (uint32_t f = 0; f < ctx->frame_count; f++) {
                dst[(size_t)f * nch + ch] = src[f];
            }
        } else {
            for (uint32_t f = 0; f < ctx->frame_count; f++) {
                dst[(size_t)f * nch + ch] = 0.0f;
            }
        }
    }
    out->frame_count = ctx->frame_count;
    return ORPHEUS_OK;
}

static int set_param(void* state, const char* id, const OrpheusValue* v) { (void)state; (void)id; (void)v; return ORPHEUS_ERR_UNSUPPORTED; }
static int get_param(void* state, const char* id, OrpheusValue* v) {
    InterleaveState* s = (InterleaveState*)state;
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
