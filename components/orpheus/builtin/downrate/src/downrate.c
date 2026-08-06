#include "orpheus_downrate.h"

#include <stdlib.h>
#include <string.h>

static const OrpheusParameter params[] = {
    { .id = "factor", .name = "Factor", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 4 },
      .min_i32 = 1, .max_i32 = 64, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
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
    "orpheus.builtin.downrate", "1.0.0", ORPHEUS_ABI_VERSION,
    ports, 2, params, 2, sizeof(DownrateState), 0, 8, 0, true, false
};

static const OrpheusComponentDescriptor* get_desc(void) { return &desc; }
static int create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) { *state = config->state_block; return ORPHEUS_OK; }
    *state = calloc(1, sizeof(DownrateState)); return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int destroy(void* state) { (void)state; return ORPHEUS_OK; } /* v2：内存由 Runtime 统一管理 */
static int prepare(void* state, const OrpheusConfig* config) {
    DownrateState* s = (DownrateState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    s->factor = 4;
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], "factor") == 0 &&
            config->param_values[i].type == ORPHEUS_VALUE_INT) {
            s->factor = (uint32_t)config->param_values[i].value.i32;
        }
    }
    if (s->factor < 1) s->factor = 1;
    s->offset_frames = 0;
    return ORPHEUS_OK;
}
static int reset(void* state) { ((DownrateState*)state)->offset_frames = 0; return ORPHEUS_OK; }

static int process(void* state, const OrpheusProcessContext* ctx) {
    DownrateState* s = (DownrateState*)state;
    if (ctx->input_count < 1 || !ctx->inputs[0]) return ORPHEUS_ERR_INVALID_ARG;
    if (ctx->output_count < 1 || !ctx->outputs[0]) return ORPHEUS_ERR_INVALID_ARG;
    const float* in = (const float*)ctx->inputs[0]->data;
    OrpheusBuffer* out = ctx->outputs[0];
    float* dst = (float*)out->data;
    uint32_t ch = s->channels;
    uint32_t frames = ctx->frame_count;

    memcpy(dst + (size_t)s->offset_frames * ch, in, (size_t)frames * ch * sizeof(float));
    s->offset_frames += frames;
    if (s->offset_frames >= out->frame_capacity) {
        s->offset_frames = 0;
        out->frame_count = out->frame_capacity;
    }
    return ORPHEUS_OK;
}

static int set_param(void* state, const char* id, const OrpheusValue* v) { (void)state; (void)id; (void)v; return ORPHEUS_ERR_UNSUPPORTED; }
static int get_param(void* state, const char* id, OrpheusValue* v) {
    DownrateState* s = (DownrateState*)state;
    if (strcmp(id, "factor") == 0) { v->type = ORPHEUS_VALUE_INT; v->value.i32 = (int32_t)s->factor; return ORPHEUS_OK; }
    if (strcmp(id, "channels") == 0) { v->type = ORPHEUS_VALUE_INT; v->value.i32 = (int32_t)s->channels; return ORPHEUS_OK; }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    DownrateState* s = (DownrateState*)state;
    ORPHEUS_REG_SLOT(reg, s, factor, ORPHEUS_SLOT_SETTING, "factor", "降采样因子",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=64,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    return ORPHEUS_OK;
}
static const OrpheusComponentInterface iface = {
    get_desc, create, destroy, prepare, reset, process, set_param, get_param, NULL, register_slots
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &iface; }
