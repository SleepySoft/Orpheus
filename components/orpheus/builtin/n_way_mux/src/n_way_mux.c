#include "orpheus_n_way_mux.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static float read_float(const OrpheusConfig* config, const char* id, float fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_FLOAT) return config->param_values[i].value.f32;
            if (config->param_values[i].type == ORPHEUS_VALUE_INT) return (float)config->param_values[i].value.i32;
        }
    }
    return fallback;
}
static int read_int(const OrpheusConfig* config, const char* id, int fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_INT) return (int)config->param_values[i].value.i32;
            if (config->param_values[i].type == ORPHEUS_VALUE_FLOAT) return (int)config->param_values[i].value.f32;
        }
    }
    return fallback;
}

static const OrpheusParameter params[] = {
    { .id = "inputs", .name = "Inputs", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "channels", .name = "Channels", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "select", .name = "Select", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.0f },
      .min_f32 = 0.0f, .max_f32 = 31.0f, .update_policy = ORPHEUS_UPDATE_SMOOTHED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "ramp_ms", .name = "Ramp ms", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 20.0f },
      .min_f32 = 0.0f, .max_f32 = 1000.0f, .unit = "ms",
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = false }
};

/* 'in' is a variable-count port: expanded to in0..inN-1 at compile time,
   unconnected pins arrive as NULL. out is one N-channel buffer. */
static const OrpheusPort ports[] = {
    { "in", ORPHEUS_PORT_INPUT,  ORPHEUS_PORT_AUDIO, ORPHEUS_FORMAT_F32, 0, 0, 0, true, "channels" },
    { "out", ORPHEUS_PORT_OUTPUT, ORPHEUS_PORT_AUDIO, ORPHEUS_FORMAT_F32, 0, 0, 0, true, "channels" }
};

static const OrpheusComponentDescriptor desc = {
    "orpheus.builtin.n_way_mux", "1.0.0", ORPHEUS_ABI_VERSION,
    ports, 2, params, 4, sizeof(NWayMuxState), 0, 8, 0, true, false
};

static const OrpheusComponentDescriptor* get_desc(void) { return &desc; }

static int create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) { *state = config->state_block; return ORPHEUS_OK; }
    *state = calloc(1, sizeof(NWayMuxState)); return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int destroy(void* state) { (void)state; return ORPHEUS_OK; }

static int prepare(void* state, const OrpheusConfig* config) {
    NWayMuxState* s = (NWayMuxState*)state;
    s->channels = (uint32_t)(config->channels > 0 ? config->channels : 2);
    s->inputs = (uint32_t)read_int(config, "inputs", 2);
    if (s->inputs < 1) s->inputs = 1;
    if (s->inputs > 32) s->inputs = 32;
    float select = read_float(config, "select", 0.0f);
    if (select < 0.0f) select = 0.0f;
    if (select > (float)(s->inputs - 1)) select = (float)(s->inputs - 1);
    s->select = select; s->select_smoothed = select;
    float ramp_ms = read_float(config, "ramp_ms", 20.0f);
    s->ramp_coeff = 1.0f;
    if (ramp_ms > 0.0f && config->sample_rate > 0) {
        float tau = ramp_ms / 1000.0f;
        s->ramp_coeff = 1.0f - expf(-1.0f / (tau * (float)config->sample_rate));
        if (s->ramp_coeff > 1.0f) s->ramp_coeff = 1.0f;
    }
    return ORPHEUS_OK;
}
static int reset(void* state) {
    NWayMuxState* s = (NWayMuxState*)state;
    s->select_smoothed = s->select;
    return ORPHEUS_OK;
}

/* 在「整数选中的输入」与其相邻路之间做一阶平滑交叉淡化，避免切换咔嘐声；未连接的输入为 NULL，跳过不参与淡化。 */
static int process(void* state, const OrpheusProcessContext* ctx) {
    NWayMuxState* s = (NWayMuxState*)state;
    OrpheusBuffer* out = ctx->outputs[0];
    if (!out) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    float* out_data = (float*)out->data;

    s->select_smoothed += s->ramp_coeff * (s->select - s->select_smoothed);
    float idx = s->select_smoothed;
    if (idx < 0.0f) idx = 0.0f;
    if (idx > (float)(s->inputs - 1)) idx = (float)(s->inputs - 1);

    uint32_t a = (uint32_t)idx;
    uint32_t b = a + 1;
    float f = idx - (float)a;
    if (b >= s->inputs) { b = a; f = 0.0f; }

    const OrpheusBuffer* inA = (a < ctx->input_count) ? ctx->inputs[a] : NULL;
    const OrpheusBuffer* inB = (b < ctx->input_count) ? ctx->inputs[b] : NULL;
    if (inA && !inB) { b = a; f = 0.0f; inB = inA; }
    if (!inA && inB) { a = b; f = 0.0f; inA = inB; }

    const float* da = inA ? (const float*)inA->data : NULL;
    const float* db = inB ? (const float*)inB->data : NULL;
    const float* d0 = da ? da : db;
    if (!d0) { memset(out_data, 0, (size_t)frames * ch * sizeof(float)); }
    else if (!db || f == 0.0f) {
        memcpy(out_data, d0, (size_t)frames * ch * sizeof(float));
    } else {
        float wb = f, wa = 1.0f - f;
        for (uint32_t i = 0; i < frames * ch; ++i) out_data[i] = da[i] * wa + db[i] * wb;
    }
    out->frame_count = frames;
    return ORPHEUS_OK;
}

static int set_param(void* state, const char* id, const OrpheusValue* v) {
    NWayMuxState* s = (NWayMuxState*)state;
    if (strcmp(id, "select") == 0) {
        if (v->type != ORPHEUS_VALUE_FLOAT && v->type != ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        float sel = (v->type == ORPHEUS_VALUE_FLOAT) ? v->value.f32 : (float)v->value.i32;
        if (sel < 0.0f) sel = 0.0f;
        if (sel > (float)(s->inputs - 1)) sel = (float)(s->inputs - 1);
        s->select = sel;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int get_param(void* state, const char* id, OrpheusValue* v) {
    NWayMuxState* s = (NWayMuxState*)state;
    if (strcmp(id, "select") == 0) { v->type = ORPHEUS_VALUE_FLOAT; v->value.f32 = s->select; return ORPHEUS_OK; }
    if (strcmp(id, "channels") == 0) { v->type = ORPHEUS_VALUE_INT; v->value.i32 = (int32_t)s->channels; return ORPHEUS_OK; }
    if (strcmp(id, "inputs") == 0) { v->type = ORPHEUS_VALUE_INT; v->value.i32 = (int32_t)s->inputs; return ORPHEUS_OK; }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    NWayMuxState* s = (NWayMuxState*)state;
    ORPHEUS_REG_SLOT(reg, s, inputs, ORPHEUS_SLOT_SETTING, "inputs", "\u8f93\u5165\u8def\u6570",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "\u901a\u9053\u6570",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, select, ORPHEUS_SLOT_SETTING, "select", "\u9009\u62e9\u8f93\u5165",
                     ORPHEUS_VALUE_FLOAT, .min_f32=0.0f, .max_f32=31.0f,
                     .update_policy=ORPHEUS_UPDATE_SMOOTHED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, select_smoothed, ORPHEUS_SLOT_READBACK, "select_smoothed",
                     "\u5f53\u524d\u9009\u62e9", ORPHEUS_VALUE_FLOAT,
                     .update_policy=ORPHEUS_UPDATE_IMMEDIATE,
                     .flags=ORPHEUS_SLOT_READBACK);
/* ramp_ms 在 C 没有独立槽（仅 prepare 用），不注册。 */
    return ORPHEUS_OK;
}
static const OrpheusComponentInterface iface = {
    get_desc, create, destroy, prepare, reset, process, set_param, get_param, NULL, register_slots
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &iface; }
