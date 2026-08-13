#include "orpheus_anc_fxlms.h"

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

static void anc_free(AncFxLmsState* s) {
    free(s->w); free(s->xbuf); free(s->xf); free(s->xfd); free(s->pos);
    s->w = s->xbuf = s->xf = s->xfd = NULL; s->pos = NULL;
}

static int anc_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) { *state = config->state_block; return ORPHEUS_OK; }
    *state = calloc(1, sizeof(AncFxLmsState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int anc_destroy(void* state) { anc_free((AncFxLmsState*)state); return ORPHEUS_OK; }

static int anc_prepare(void* state, const OrpheusConfig* config) {
    AncFxLmsState* s = (AncFxLmsState*)state;
    anc_free(s);
    memset(s, 0, sizeof(*s));
    s->channels = config->channels > 0 ? (config->channels < ANC_FXLMS_MAX_CH ? config->channels : ANC_FXLMS_MAX_CH) : 2;
    s->w_len = (uint32_t)read_int_param(config, "filter_length", 128);
    if (s->w_len < 1) s->w_len = 128;
    if (s->w_len > ANC_FXLMS_MAX_W) s->w_len = ANC_FXLMS_MAX_W;
    s->mu   = read_float_param(config, "step_size", 0.05f);
    s->leak = read_float_param(config, "leakage", 1.0f);
    s->eps  = read_float_param(config, "eps", 1e-6f);
    s->s_gain  = read_float_param(config, "secondary_gain", 1.0f);
    s->s_delay = read_float_param(config, "secondary_delay", 3.0f);

    size_t ch = s->channels, L = s->w_len;
    s->w    = (float*)calloc(ch * L, sizeof(float));
    s->xbuf = (float*)calloc(ch * L, sizeof(float));
    s->xf   = (float*)calloc(ch * L, sizeof(float));
    s->pos  = (uint32_t*)calloc(ch, sizeof(uint32_t));
    s->xfd  = (float*)calloc(ch, sizeof(float));
    if (!s->w || !s->xbuf || !s->xf || !s->pos || !s->xfd) { anc_free(s); return ORPHEUS_ERR_OUT_OF_MEMORY; }
    return ORPHEUS_OK;
}

static int anc_reset(void* state) {
    AncFxLmsState* s = (AncFxLmsState*)state;
    size_t ch = s->channels, L = s->w_len;
    if (s->w) memset(s->w, 0, ch * L * sizeof(float));
    if (s->xbuf) memset(s->xbuf, 0, ch * L * sizeof(float));
    if (s->xf) memset(s->xf, 0, ch * L * sizeof(float));
    if (s->xfd) memset(s->xfd, 0, ch * sizeof(float));
    if (s->pos) memset(s->pos, 0, ch * sizeof(uint32_t));
    s->acc_px2 = 0; s->acc_d2 = 0; s->acc_e2 = 0;
    s->noise_reduction_db = 0; s->total_frames = 0; s->power_d = 0; s->power_e = 0;
    return ORPHEUS_OK;
}

/*
 * FxLMS?Filtered-x Least Mean Squares???????
 *
 *   x[n]   ?????????????
 *   d[n]   ????????????
 *   w      ???????? x ???????????
 *   y[n]  = w^T x[n]                      ????
 *   out    = -y[n]                        ????????????
 *   e[n]  = d[n] - g*y[n-?]               ????????????
 *
 * ?????? S(z)=g?z^{-?}??????????????????
 * ?? g + ?? ??Filtered-x ?????????? S ????
 *   xf[n][k] = g * x[n-?-k]                ????????? ? ???
 *
 * ????????? LMS?? x ???
 *   w[n+1] = w[n] + mu*e[n]*xf[n] / (||xf[n]||^2 + eps)
 */
static int anc_process(void* state, const OrpheusProcessContext* ctx) {
    AncFxLmsState* s = (AncFxLmsState*)state;
    if (ctx->input_count < 2 || !ctx->inputs[0] || !ctx->inputs[1]) return ORPHEUS_ERR_INVALID_ARG;
    if (ctx->output_count < 1 || !ctx->outputs[0]) return ORPHEUS_ERR_INVALID_ARG;
    const float* x = (const float*)ctx->inputs[0]->data;
    const float* d = (const float*)ctx->inputs[1]->data;
    float* out = (float*)ctx->outputs[0]->data;
    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    uint32_t L = s->w_len;
    float g = s->s_gain;
    uint32_t D = (uint32_t)(s->s_delay >= 0 ? s->s_delay : 0);

    double px2 = 0, pd = 0, pe = 0;

    for (uint32_t c = 0; c < ch; ++c) {
        float* wc  = s->w    + (size_t)c * L;
        float* xc  = s->xbuf + (size_t)c * L;
        float* xfc = s->xf   + (size_t)c * L;
        uint32_t p = s->pos[c];

        for (uint32_t n = 0; n < frames; ++n) {
            /* ???????????? filtered-x ????? D ? ? g? */
            xc[p] = x[n * ch + c];
            float xfn = 0.0f, xfnorm = 0.0f;
            for (uint32_t k = 0; k < L; ++k) {
                uint32_t kd = (p + L - k) % L;
                uint32_t fk = (p + L - ((k + D) % L)) % L;  /* ?? D ????? */
                float xfv = g * xc[fk];
                xfc[kd] = xfv;              /* filtered-x ???????? */
                xfn   += wc[k] * xfv;       /* ? filtered-x ??????? */
                xfnorm+= xfv * xfv;
            }
            /* ???? y = w^T x??????? */
            float y = 0.0f;
            for (uint32_t k = 0; k < L; ++k) {
                uint32_t idx = (p + L - k) % L;
                y += wc[k] * xc[idx];
            }
            /* ?? mic?d[n] - g*y[n-D]?????? y ??? S? */
            float dn = d[n * ch + c];
            float y_d = 0.0f;
            {
                /* y ????? D ????????????????????? y ???
                   ???????????? filtered-x ??????????????
                   ? g ?????????? */
                y_d = g * y;
            }
            float e = dn - y_d;

            px2 += (double)(xc[p] * xc[p]);
            pd  += (double)(dn * dn);
            pe  += (double)(e * e);

            /* w ???? filtered-x ?????????? */
            float sc = s->mu * e / (xfnorm + s->eps);
            for (uint32_t k = 0; k < L; ++k) {
                uint32_t kd = (p + L - k) % L;
                wc[k] = s->leak * wc[k] + sc * xfc[kd];
            }

            out[n * ch + c] = -y;   /* ?????? */
            p = (p + 1) % L;
        }
        s->pos[c] = p;
    }

    s->acc_px2 += px2; s->acc_d2 += pd; s->acc_e2 += pe;
    s->total_frames++;
    double ns = (double)(s->total_frames * frames * ch);
    s->power_d = ns > 0 ? (float)(s->acc_d2 / ns) : 0.0f;
    s->power_e = ns > 0 ? (float)(s->acc_e2 / ns) : 0.0f;
    s->noise_reduction_db = (s->power_e > 1e-30f) ? 10.0f * log10f(s->power_d / (s->power_e + 1e-30f)) : 0.0f;

    {
        char* p = s->json_detail; size_t rem = sizeof(s->json_detail);
        int len = snprintf(p, rem,
            "{\"noise_reduction_db\":%.2f,\"power_d\":%.6g,\"power_e\":%.6g,\"frames\":%llu}",
            (double)s->noise_reduction_db, (double)s->power_d, (double)s->power_e,
            (unsigned long long)s->total_frames);
        (void)len;
    }
    if (ctx->outputs[0]->frame_capacity >= frames) ctx->outputs[0]->frame_count = frames;
    return ORPHEUS_OK;
}

static int anc_set_parameter(void* state, const char* id, const OrpheusValue* v) {
    (void)state;(void)id;(void)v;
    return ORPHEUS_ERR_UNSUPPORTED;
}
static int anc_get_parameter(void* state, const char* id, OrpheusValue* v) {
    AncFxLmsState* s = (AncFxLmsState*)state;
    if (!strcmp(id, "channels")) { v->type = ORPHEUS_VALUE_INT; v->value.i32 = (int32_t)s->channels; return ORPHEUS_OK; }
    if (!strcmp(id, "filter_length")) { v->type = ORPHEUS_VALUE_INT; v->value.i32 = (int32_t)s->w_len; return ORPHEUS_OK; }
    if (!strcmp(id, "step_size")) { v->type = ORPHEUS_VALUE_FLOAT; v->value.f32 = s->mu; return ORPHEUS_OK; }
    if (!strcmp(id, "noise_reduction_db")) { v->type = ORPHEUS_VALUE_FLOAT; v->value.f32 = s->noise_reduction_db; return ORPHEUS_OK; }
    if (!strcmp(id, "power_d")) { v->type = ORPHEUS_VALUE_FLOAT; v->value.f32 = s->power_d; return ORPHEUS_OK; }
    if (!strcmp(id, "power_e")) { v->type = ORPHEUS_VALUE_FLOAT; v->value.f32 = s->power_e; return ORPHEUS_OK; }
    if (!strcmp(id, "detail")) { v->type = ORPHEUS_VALUE_STRING; v->value.str = s->json_detail; return ORPHEUS_OK; }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int anc_register_slots(void* state, const OrpheusRegistry* reg) {
    AncFxLmsState* s = (AncFxLmsState*)state;
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "\u901a\u9053\u6570", ORPHEUS_VALUE_INT,
        .min_i32=1, .max_i32=8, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
        .flags=ORPHEUS_SLOT_PERSISTENT|ORPHEUS_SLOT_READBACK|ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, w_len, ORPHEUS_SLOT_SETTING, "filter_length", "\u6ee4\u6ce2\u5668\u9636\u6570", ORPHEUS_VALUE_INT,
        .min_i32=1, .max_i32=1024, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
        .flags=ORPHEUS_SLOT_PERSISTENT|ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, mu, ORPHEUS_SLOT_SETTING, "step_size", "\u6b65\u957f", ORPHEUS_VALUE_FLOAT,
        .min_f32=0.0f, .max_f32=2.0f, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
        .flags=ORPHEUS_SLOT_PERSISTENT|ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, noise_reduction_db, ORPHEUS_SLOT_PROBE, "noise_reduction_db", "\u964d\u566a\u91cf(dB)", ORPHEUS_VALUE_FLOAT,
        .flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, power_d, ORPHEUS_SLOT_PROBE, "power_d", "\u6b8b\u5dee\u9ea6\u514b\u98ce\u529f\u7387", ORPHEUS_VALUE_FLOAT,
        .flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, power_e, ORPHEUS_SLOT_PROBE, "power_e", "\u6d88\u9664\u540e\u529f\u7387", ORPHEUS_VALUE_FLOAT,
        .flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, json_detail, ORPHEUS_SLOT_PROBE, "detail", "\u660e\u7ec6", ORPHEUS_VALUE_STRING,
        .flags=ORPHEUS_SLOT_READBACK);
    return ORPHEUS_OK;
}

static const OrpheusParameter anc_params[] = {
    { .id="channels", .name="\u901a\u9053\u6570", .type=ORPHEUS_VALUE_INT,
      .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=2}, .min_i32=1,.max_i32=8,
      .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true,.affects_signature=true },
    { .id="filter_length", .name="\u6ee4\u6ce2\u5668\u9636\u6570", .type=ORPHEUS_VALUE_INT,
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
    { .id="secondary_gain", .name="\u6b21\u8def\u5f94\u76ca", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=1.0f}, .min_f32=0.0f,.max_f32=10.0f,
      .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true,.affects_signature=false },
    { .id="secondary_delay", .name="\u6b21\u8def\u5ef6\u8fdf(\u91c7\u6837\u70b9)", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=3.0f}, .min_f32=0.0f,.max_f32=512.0f,
      .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true,.affects_signature=false },
    { .id="noise_reduction_db", .name="\u964d\u566a\u91cf(dB)", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=0.0f},
      .update_policy=ORPHEUS_UPDATE_IMMEDIATE,.readback=true,.persistent=false,.affects_signature=false },
    { .id="power_d", .name="\u6b8b\u5dee\u9ea6\u514b\u98ce\u529f\u7387", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=0.0f},
      .update_policy=ORPHEUS_UPDATE_IMMEDIATE,.readback=true,.persistent=false,.affects_signature=false },
    { .id="power_e", .name="\u6d88\u9664\u540e\u529f\u7387", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=0.0f},
      .update_policy=ORPHEUS_UPDATE_IMMEDIATE,.readback=true,.persistent=false,.affects_signature=false },
    { .id="detail", .name="\u660e\u7ec6", .type=ORPHEUS_VALUE_STRING,
      .default_value={.type=ORPHEUS_VALUE_STRING,.value.str="{}"},
      .update_policy=ORPHEUS_UPDATE_IMMEDIATE,.readback=true,.persistent=false,.affects_signature=false }
};
static const OrpheusPort anc_ports[] = {
    { .id="x", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32,
      .channels=0,.sample_rate=0,.block_size=0,.is_variable=true,.channels_param="channels" },
    { .id="d", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32,
      .channels=0,.sample_rate=0,.block_size=0,.is_variable=true,.channels_param="channels" },
    { .id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32,
      .channels=0,.sample_rate=0,.block_size=0,.is_variable=true,.channels_param="channels" }
};
static const OrpheusComponentDescriptor anc_descriptor = {
    .id="orpheus.builtin.anc_fxlms", .version="1.0.0", .abi_version=ORPHEUS_ABI_VERSION,
    .ports=anc_ports, .port_count=3, .params=anc_params, .param_count=11,
    .state_size=sizeof(AncFxLmsState), .scratch_size=0, .alignment=8,
    .latency_samples=0, .realtime_safe=true, .supports_inplace=false
};
static const OrpheusComponentDescriptor* anc_get_descriptor(void){ return &anc_descriptor; }

static const OrpheusComponentInterface anc_interface = {
    .get_descriptor=anc_get_descriptor,.create=anc_create,.destroy=anc_destroy,
    .prepare=anc_prepare,.reset=anc_reset,.process=anc_process,
    .set_parameter=anc_set_parameter,.get_parameter=anc_get_parameter,
    .get_state_value=NULL,.register_slots=anc_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void){ return &anc_interface; }
