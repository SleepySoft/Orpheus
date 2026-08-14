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
            if (config->param_values[i].type == ORPHEUS_VALUE_FLOAT) return (int)(config->param_values[i].value.f32 + 0.5f);
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
    { .id = "select", .name = "Select", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 1 },
      .min_i32 = 1, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_SMOOTHED,
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
    "orpheus.builtin.n_way_mux", "1.1.0", ORPHEUS_ABI_VERSION,
    ports, 2, params, 4, sizeof(NWayMuxState), 0, 8, 0, true, false
};

static const OrpheusComponentDescriptor* get_desc(void) { return &desc; }

static int create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) { *state = config->state_block; return ORPHEUS_OK; }
    *state = calloc(1, sizeof(NWayMuxState)); return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int destroy(void* state) { (void)state; return ORPHEUS_OK; }

static uint32_t clamp_select(int sel, uint32_t inputs) {
    if (sel < 1) sel = 1;
    if ((uint32_t)sel > inputs) sel = (int)inputs;
    return (uint32_t)sel;
}

static int prepare(void* state, const OrpheusConfig* config) {
    NWayMuxState* s = (NWayMuxState*)state;
    s->channels = (uint32_t)(config->channels > 0 ? config->channels : 2);
    s->inputs = (uint32_t)read_int(config, "inputs", 2);
    if (s->inputs < 1) s->inputs = 1;
    if (s->inputs > 32) s->inputs = 32;
    s->select = clamp_select(read_int(config, "select", 1), s->inputs);
    s->select_pos = (float)(s->select - 1);
    /* ramp_ms 线性淡化时长（0 = 立即切换）；步长覆盖最大跳变（32 路）所需 */
    float ramp_ms = read_float(config, "ramp_ms", 20.0f);
    if (ramp_ms > 0.0f && config->sample_rate > 0) {
        float ramp_samples = ramp_ms / 1000.0f * (float)config->sample_rate;
        s->ramp_step = 1.0f / ramp_samples;
    } else {
        s->ramp_step = 32.0f; /* 一步到达任意目标 */
    }
    return ORPHEUS_OK;
}
static int reset(void* state) {
    NWayMuxState* s = (NWayMuxState*)state;
    s->select_pos = (float)(s->select - 1);
    return ORPHEUS_OK;
}

/* select 为 1 起整数；切换时位置按 ramp_ms 线性滑动，相邻两路交叉淡化，无咔哒声。
   未连接的输入为 NULL：淡出/淡入到静音。 */
static int process(void* state, const OrpheusProcessContext* ctx) {
    NWayMuxState* s = (NWayMuxState*)state;
    OrpheusBuffer* out = ctx->outputs[0];
    if (!out) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    float* out_data = (float*)out->data;
    float target = (float)(s->select - 1);

    /* 稳态直通：位置已落在整数输入上且无过渡，整帧拷贝 */
    float pos = s->select_pos;
    if (pos == target) {
        uint32_t a = (uint32_t)(pos + 0.5f);
        const OrpheusBuffer* inA = (a < ctx->input_count) ? ctx->inputs[a] : NULL;
        if (inA) memcpy(out_data, inA->data, (size_t)frames * ch * sizeof(float));
        else memset(out_data, 0, (size_t)frames * ch * sizeof(float));
        out->frame_count = frames;
        return ORPHEUS_OK;
    }

    for (uint32_t n = 0; n < frames; ++n) {
        if (pos < target) { pos += s->ramp_step; if (pos > target) pos = target; }
        else if (pos > target) { pos -= s->ramp_step; if (pos < target) pos = target; }

        uint32_t a = (uint32_t)pos;
        uint32_t b = a + 1;
        float f = pos - (float)a;
        if (b >= s->inputs) { b = a; f = 0.0f; }
        const OrpheusBuffer* inA = (a < ctx->input_count) ? ctx->inputs[a] : NULL;
        const OrpheusBuffer* inB = (b < ctx->input_count) ? ctx->inputs[b] : NULL;
        const float* da = inA ? (const float*)inA->data : NULL;
        const float* db = inB ? (const float*)inB->data : NULL;
        float wa = 1.0f - f, wb = f;
        float* o = out_data + (size_t)n * ch;
        const float* pa = da ? da + (size_t)n * ch : NULL;
        const float* pb = db ? db + (size_t)n * ch : NULL;
        if (pa && pb)      for (uint32_t c = 0; c < ch; ++c) o[c] = pa[c] * wa + pb[c] * wb;
        else if (pa)       for (uint32_t c = 0; c < ch; ++c) o[c] = pa[c] * wa;
        else if (pb)       for (uint32_t c = 0; c < ch; ++c) o[c] = pb[c] * wb;
        else               for (uint32_t c = 0; c < ch; ++c) o[c] = 0.0f;
    }
    s->select_pos = pos;
    out->frame_count = frames;
    return ORPHEUS_OK;
}

static int set_param(void* state, const char* id, const OrpheusValue* v) {
    NWayMuxState* s = (NWayMuxState*)state;
    if (strcmp(id, "select") == 0) {
        if (v->type != ORPHEUS_VALUE_FLOAT && v->type != ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        int sel = (v->type == ORPHEUS_VALUE_INT) ? (int)v->value.i32 : (int)(v->value.f32 + 0.5f);
        s->select = clamp_select(sel, s->inputs > 0 ? s->inputs : 1);
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int get_param(void* state, const char* id, OrpheusValue* v) {
    NWayMuxState* s = (NWayMuxState*)state;
    if (strcmp(id, "select") == 0) { v->type = ORPHEUS_VALUE_INT; v->value.i32 = (int32_t)s->select; return ORPHEUS_OK; }
    if (strcmp(id, "channels") == 0) { v->type = ORPHEUS_VALUE_INT; v->value.i32 = (int32_t)s->channels; return ORPHEUS_OK; }
    if (strcmp(id, "inputs") == 0) { v->type = ORPHEUS_VALUE_INT; v->value.i32 = (int32_t)s->inputs; return ORPHEUS_OK; }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    NWayMuxState* s = (NWayMuxState*)state;
    ORPHEUS_REG_SLOT(reg, s, inputs, ORPHEUS_SLOT_SETTING, "inputs", "输入路数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, select, ORPHEUS_SLOT_SETTING, "select", "选择输入",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_SMOOTHED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, select_pos, ORPHEUS_SLOT_READBACK, "select_pos",
                     "当前位置", ORPHEUS_VALUE_FLOAT,
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
