#include "orpheus_resample.h"

#include <stdlib.h>
#include <string.h>

static const OrpheusParameter params[] = {
    { .id = "mode", .name = "抽取方式", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 0 },
      .min_i32 = 0, .max_i32 = 1, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true },
    { .id = "phase", .name = "选点相位", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 0 },
      .min_i32 = 0, .max_i32 = 63, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true },
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
    "orpheus.builtin.resample", "1.1.0", ORPHEUS_ABI_VERSION,
    ports, 2, params, 4, sizeof(ResampleState), 0, 8, 0, true, false
};

static const OrpheusComponentDescriptor* get_desc(void) { return &desc; }
static int create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) { *state = config->state_block; return ORPHEUS_OK; }
    *state = calloc(1, sizeof(ResampleState)); return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int destroy(void* state) { (void)state; return ORPHEUS_OK; } /* v2：内存由 Runtime 统一管理 */
static int reset(void* state);
static int prepare(void* state, const OrpheusConfig* config) {
    if (!state || !config) return ORPHEUS_ERR_INVALID_ARG;
    ResampleState* s = (ResampleState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    s->factor = 2;
    s->mode = 0;
    s->phase = 0;
    for (uint32_t i = 0; i < config->param_count; ++i) {
        const char* id = config->param_ids[i];
        if (!id) continue;
        if (strcmp(id, "factor") != 0 && strcmp(id, "mode") != 0 &&
            strcmp(id, "phase") != 0) continue;
        const OrpheusValue* value = &config->param_values[i];
        if (value->type != ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (strcmp(id, "factor") == 0) s->factor = (uint32_t)value->value.i32;
        else if (strcmp(id, "phase") == 0) s->phase = (uint32_t)value->value.i32;
        else s->mode = value->value.i32;
    }
    if (s->channels > RESAMPLE_MAX_CHANNELS || s->factor < 2 || s->factor > 64 ||
        s->mode < 0 || s->mode > 1 || s->phase >= s->factor)
        return ORPHEUS_ERR_INVALID_ARG;
    return reset(state);
}
static int reset(void* state) {
    ResampleState* s = (ResampleState*)state;
    memset(s->acc, 0, sizeof(s->acc));
    s->n = 0;
    s->write_pos = 0;
    return ORPHEUS_OK;
}

static int process(void* state, const OrpheusProcessContext* ctx) {
    if (!state || !ctx) return ORPHEUS_ERR_INVALID_ARG;
    ResampleState* s = (ResampleState*)state;
    if (ctx->input_count < 1 || !ctx->inputs[0]) return ORPHEUS_ERR_INVALID_ARG;
    if (ctx->output_count < 1 || !ctx->outputs[0]) return ORPHEUS_ERR_INVALID_ARG;
    const float* in = (const float*)ctx->inputs[0]->data;
    OrpheusBuffer* out = ctx->outputs[0];
    const OrpheusBuffer* input = ctx->inputs[0];
    if (!in || !out->data || input->channels != s->channels ||
        out->channels != s->channels || !input->interleaved || !out->interleaved ||
        input->format != ORPHEUS_FORMAT_F32 || out->format != ORPHEUS_FORMAT_F32 ||
        ctx->frame_count > input->frame_capacity || out->frame_capacity == 0 ||
        s->write_pos >= out->frame_capacity) return ORPHEUS_ERR_INVALID_ARG;
    float* dst = (float*)out->data;
    uint32_t ch = s->channels;
    uint32_t n = s->factor;

    for (uint32_t f = 0; f < ctx->frame_count; ++f) {
        if (s->mode == 0) {
            for (uint32_t c = 0; c < ch; ++c)
                s->acc[c] += in[(size_t)f * ch + c];
        } else if (s->n == s->phase) {
            memcpy(s->acc, in + (size_t)f * ch, ch * sizeof(float));
        }
        if (++s->n >= n) {
            if (s->mode == 0) {
                for (uint32_t c = 0; c < ch; ++c) {
                    dst[(size_t)s->write_pos * ch + c] = s->acc[c] / (float)n;
                    s->acc[c] = 0.0f;
                }
            } else {
                memcpy(dst + (size_t)s->write_pos * ch, s->acc, ch * sizeof(float));
            }
            s->n = 0;
            if (++s->write_pos >= out->frame_capacity) {
                s->write_pos = 0;
                out->frame_count = out->frame_capacity;
            }
        }
    }
    return orpheus_process_next(ctx);
}

static int set_param(void* state, const char* id, const OrpheusValue* v) { (void)state; (void)id; (void)v; return ORPHEUS_ERR_UNSUPPORTED; }
static int get_param(void* state, const char* id, OrpheusValue* v) {
    ResampleState* s = (ResampleState*)state;
    if (strcmp(id, "factor") == 0) { v->type = ORPHEUS_VALUE_INT; v->value.i32 = (int32_t)s->factor; return ORPHEUS_OK; }
    if (strcmp(id, "channels") == 0) { v->type = ORPHEUS_VALUE_INT; v->value.i32 = (int32_t)s->channels; return ORPHEUS_OK; }
    if (strcmp(id, "mode") == 0) { v->type = ORPHEUS_VALUE_INT; v->value.i32 = s->mode; return ORPHEUS_OK; }
    if (strcmp(id, "phase") == 0) { v->type = ORPHEUS_VALUE_INT; v->value.i32 = (int32_t)s->phase; return ORPHEUS_OK; }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    ResampleState* s = (ResampleState*)state;
    ORPHEUS_REG_SLOT(reg, s, mode, ORPHEUS_SLOT_SETTING, "mode", "抽取方式",
                     ORPHEUS_VALUE_INT, .min_i32=0, .max_i32=1,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, phase, ORPHEUS_SLOT_SETTING, "phase", "选点相位",
                     ORPHEUS_VALUE_INT, .min_i32=0, .max_i32=63,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, factor, ORPHEUS_SLOT_SETTING, "factor", "重采样因子",
                     ORPHEUS_VALUE_INT, .min_i32=2, .max_i32=64,
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
