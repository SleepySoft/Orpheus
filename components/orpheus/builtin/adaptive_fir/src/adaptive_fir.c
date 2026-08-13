#include "orpheus_adaptive_fir.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static float read_float_param(const OrpheusConfig* config, const char* id, float fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_FLOAT) return config->param_values[i].value.f32;
            if (config->param_values[i].type == ORPHEUS_VALUE_INT) return (float)config->param_values[i].value.i32;
        }
    }
    return fallback;
}
static int read_int_param(const OrpheusConfig* config, const char* id, int fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_INT) return config->param_values[i].value.i32;
            if (config->param_values[i].type == ORPHEUS_VALUE_FLOAT) return (int)config->param_values[i].value.f32;
        }
    }
    return fallback;
}

static void afir_free(AdaptiveFirState* s) {
    free(s->w); free(s->xbuf); free(s->dbuf); free(s->pos);
    s->w = s->xbuf = s->dbuf = NULL; s->pos = NULL;
}

static int afir_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) { *state = config->state_block; return ORPHEUS_OK; }
    *state = calloc(1, sizeof(AdaptiveFirState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int afir_destroy(void* state) { afir_free((AdaptiveFirState*)state); return ORPHEUS_OK; }

static int afir_prepare(void* state, const OrpheusConfig* config) {
    AdaptiveFirState* s = (AdaptiveFirState*)state;
    afir_free(s);
    memset(s, 0, sizeof(*s));
    s->channels = config->channels > 0 ? (config->channels < AFIR_MAX_CH ? config->channels : AFIR_MAX_CH) : 2;
    s->taps = (uint32_t)read_int_param(config, "filter_length", 128);
    if (s->taps < 1) s->taps = 128;
    if (s->taps > AFIR_MAX_TAPS) s->taps = AFIR_MAX_TAPS;
    s->mu   = read_float_param(config, "step_size", 0.05f);
    s->leak = read_float_param(config, "leakage", 1.0f);
    s->eps  = read_float_param(config, "eps", 1e-6f);
    s->sg   = read_float_param(config, "secondary_gain", 1.0f);

    size_t ch = s->channels, L = s->taps;
    s->w    = (float*)calloc(ch * L, sizeof(float));
    s->xbuf = (float*)calloc(ch * L, sizeof(float));
    s->dbuf = (float*)calloc(ch * L, sizeof(float));
    s->pos  = (uint32_t*)calloc(ch, sizeof(uint32_t));
    if (!s->w || !s->xbuf || !s->dbuf || !s->pos) { afir_free(s); return ORPHEUS_ERR_OUT_OF_MEMORY; }
    return ORPHEUS_OK;
}

static int afir_reset(void* state) {
    AdaptiveFirState* s = (AdaptiveFirState*)state;
    size_t ch = s->channels, L = s->taps;
    if (s->w) memset(s->w, 0, ch * L * sizeof(float));
    if (s->xbuf) memset(s->xbuf, 0, ch * L * sizeof(float));
    if (s->dbuf) memset(s->dbuf, 0, ch * L * sizeof(float));
    if (s->pos) memset(s->pos, 0, ch * sizeof(uint32_t));
    s->total_frames = 0; s->acc_norm = 0; s->conv_metric = 0;
    return ORPHEUS_OK;
}

/* FxLMS 核心：用原始参考 x 读输出，用 filtered-x(deriv) 更新，误差 e = d - sg*y 内部计算以避免数据流环 */
static int afir_process(void* state, const OrpheusProcessContext* ctx) {
    AdaptiveFirState* s = (AdaptiveFirState*)state;
    if (ctx->input_count < 3 || !ctx->inputs[0] || !ctx->inputs[1] || !ctx->inputs[2])
        return ORPHEUS_ERR_INVALID_ARG;
    if (ctx->output_count < 1 || !ctx->outputs[0]) return ORPHEUS_ERR_INVALID_ARG;
    const float* x     = (const float*)ctx->inputs[0]->data; /* 原始参考 */
    const float* deriv = (const float*)ctx->inputs[1]->data; /* filtered-x */
    const float* err   = (const float*)ctx->inputs[2]->data; /* 误差麦 d */
    float* out = (float*)ctx->outputs[0]->data;
    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    uint32_t L = s->taps;

    double pn = 0.0, pe = 0.0;

    for (uint32_t c = 0; c < ch; ++c) {
        float* wc  = s->w    + (size_t)c * L;
        float* xc  = s->xbuf + (size_t)c * L;
        float* dc  = s->dbuf + (size_t)c * L;
        uint32_t p = s->pos[c];
        for (uint32_t n = 0; n < frames; ++n) {
            float xv = x[n * ch + c];
            float dv = deriv[n * ch + c];
            xc[p] = xv;
            dc[p] = dv;
            float y = 0.0f, dnorm = 0.0f;
            for (uint32_t k = 0; k < L; ++k) {
                uint32_t idx = (p + L - k) % L;
                y += wc[k] * xc[idx];
                dnorm += dc[idx] * dc[idx];
            }
            float dnv = err[n * ch + c];
            float e = dnv - s->sg * y;   /* 误差 e = d - g*y（误差计算关在核心内） */
            float sc = s->mu * e / (dnorm + s->eps);
            for (uint32_t k = 0; k < L; ++k) {
                uint32_t idx = (p + L - k) % L;
                wc[k] = s->leak * wc[k] + sc * dc[idx];
            }
            out[n * ch + c] = y;
            pn += (double)dnorm;
            pe += (double)(e * e);
            p = (p + 1) % L;
        }
        s->pos[c] = p;
    }
    s->total_frames++;
    s->conv_metric = (float)(sqrt(pe / ((double)frames * ch + 1e-12)));

    {
        char* b = s->json_detail; size_t rem = sizeof(s->json_detail);
        int len = snprintf(b, rem, "{\"filter_length\":%u,\"conv_metric\":%.6g}",
            (unsigned)s->taps, (double)s->conv_metric);
        (void)len;
    }
    if (ctx->outputs[0]->frame_capacity >= frames) ctx->outputs[0]->frame_count = frames;
    return ORPHEUS_OK;
}

static int afir_set_parameter(void* state, const char* id, const OrpheusValue* v) {
    (void)state;(void)id;(void)v;
    return ORPHEUS_ERR_UNSUPPORTED;
}
static int afir_get_parameter(void* state, const char* id, OrpheusValue* v) {
    AdaptiveFirState* s = (AdaptiveFirState*)state;
    if (!strcmp(id, "channels")) { v->type = ORPHEUS_VALUE_INT; v->value.i32 = (int32_t)s->channels; return ORPHEUS_OK; }
    if (!strcmp(id, "filter_length")) { v->type = ORPHEUS_VALUE_INT; v->value.i32 = (int32_t)s->taps; return ORPHEUS_OK; }
    if (!strcmp(id, "conv_metric")) { v->type = ORPHEUS_VALUE_FLOAT; v->value.f32 = s->conv_metric; return ORPHEUS_OK; }
    if (!strcmp(id, "secondary_gain")) { v->type = ORPHEUS_VALUE_FLOAT; v->value.f32 = s->sg; return ORPHEUS_OK; }
    if (!strcmp(id, "detail")) { v->type = ORPHEUS_VALUE_STRING; v->value.str = s->json_detail; return ORPHEUS_OK; }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int afir_register_slots(void* state, const OrpheusRegistry* reg) {
    AdaptiveFirState* s = (AdaptiveFirState*)state;
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "\u901a\u9053\u6570", ORPHEUS_VALUE_INT,
        .min_i32=1, .max_i32=32, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
        .flags=ORPHEUS_SLOT_PERSISTENT|ORPHEUS_SLOT_READBACK|ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, taps, ORPHEUS_SLOT_SETTING, "filter_length", "\u9636\u6570", ORPHEUS_VALUE_INT,
        .min_i32=1, .max_i32=1024, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
        .flags=ORPHEUS_SLOT_PERSISTENT|ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, sg, ORPHEUS_SLOT_SETTING, "secondary_gain", "\u6b21\u7ea7\u8def\u5f84\u5e02\u76ca", ORPHEUS_VALUE_FLOAT,
        .min_f32=0.0f, .max_f32=10.0f, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
        .flags=ORPHEUS_SLOT_PERSISTENT|ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, conv_metric, ORPHEUS_SLOT_PROBE, "conv_metric", "\u6536\u655b\u6307\u6807", ORPHEUS_VALUE_FLOAT,
        .flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, json_detail, ORPHEUS_SLOT_PROBE, "detail", "\u660e\u7ec6", ORPHEUS_VALUE_STRING,
        .flags=ORPHEUS_SLOT_READBACK);
    return ORPHEUS_OK;
}

static const OrpheusParameter afir_params[] = {
    { .id="channels", .name="\u901a\u9053\u6570", .type=ORPHEUS_VALUE_INT,
      .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=1}, .min_i32=1,.max_i32=32,
      .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true,.affects_signature=true },
    { .id="filter_length", .name="\u9636\u6570", .type=ORPHEUS_VALUE_INT,
      .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=128}, .min_i32=1,.max_i32=1024,
      .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true,.affects_signature=false },
    { .id="step_size", .name="\u6b65\u957f", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=0.05f}, .min_f32=0.0f,.max_f32=2.0f,
      .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true,.affects_signature=false },
    { .id="leakage", .name="\u6cc4\u6f0f\u56e0\u5b50", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=1.0f}, .min_f32=0.0f,.max_f32=1.0f,
      .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true,.affects_signature=false },
    { .id="eps", .name="\u6b63\u5219\u5316", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=1e-6f}, .min_f32=1e-12f,.max_f32=1.0f,
      .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true,.affects_signature=false },
    { .id="secondary_gain", .name="\u6b21\u7ea7\u8def\u5f84\u5e02\u76ca", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=1.0f}, .min_f32=0.0f,.max_f32=10.0f,
      .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true,.affects_signature=false },
    { .id="conv_metric", .name="\u6536\u655b\u6307\u6807", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=0.0f},
      .update_policy=ORPHEUS_UPDATE_IMMEDIATE,.readback=true,.persistent=false,.affects_signature=false },
    { .id="detail", .name="\u660e\u7ec6", .type=ORPHEUS_VALUE_STRING,
      .default_value={.type=ORPHEUS_VALUE_STRING,.value.str="{}"},
      .update_policy=ORPHEUS_UPDATE_IMMEDIATE,.readback=true,.persistent=false,.affects_signature=false }
};
static const OrpheusPort afir_ports[] = {
    { .id="x", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32,
      .channels=0,.sample_rate=0,.block_size=0,.is_variable=true,.channels_param="channels" },
    { .id="deriv", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32,
      .channels=0,.sample_rate=0,.block_size=0,.is_variable=true,.channels_param="channels" },
    { .id="err", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32,
      .channels=0,.sample_rate=0,.block_size=0,.is_variable=true,.channels_param="channels" },
    { .id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32,
      .channels=0,.sample_rate=0,.block_size=0,.is_variable=true,.channels_param="channels" }
};
static const OrpheusComponentDescriptor afir_descriptor = {
    .id="orpheus.builtin.adaptive_fir", .version="1.0.0", .abi_version=ORPHEUS_ABI_VERSION,
    .ports=afir_ports, .port_count=4, .params=afir_params, .param_count=8,
    .state_size=sizeof(AdaptiveFirState), .scratch_size=0, .alignment=8,
    .latency_samples=0, .realtime_safe=true, .supports_inplace=false
};
static const OrpheusComponentDescriptor* afir_get_descriptor(void){ return &afir_descriptor; }

static const OrpheusComponentInterface afir_interface = {
    .get_descriptor=afir_get_descriptor,.create=afir_create,.destroy=afir_destroy,
    .prepare=afir_prepare,.reset=afir_reset,.process=afir_process,
    .set_parameter=afir_set_parameter,.get_parameter=afir_get_parameter,
    .get_state_value=NULL,.register_slots=afir_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void){ return &afir_interface; }
