#include "orpheus_noise_detector_nlms.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int read_int_param(const OrpheusConfig* config, const char* id, int fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_INT) return config->param_values[i].value.i32;
            if (config->param_values[i].type == ORPHEUS_VALUE_FLOAT) return (int)config->param_values[i].value.f32;
        }
    }
    return fallback;
}
static float read_float_param(const OrpheusConfig* config, const char* id, float fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_FLOAT) return config->param_values[i].value.f32;
            if (config->param_values[i].type == ORPHEUS_VALUE_INT) return (float)config->param_values[i].value.i32;
        }
    }
    return fallback;
}

static void ndnlms_free(NoiseDetectorNlmsState* s) {
    free(s->w); free(s->x); free(s->pos);
    s->w = NULL; s->x = NULL; s->pos = NULL;
}

static int ndnlms_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) { *state = config->state_block; return ORPHEUS_OK; }
    *state = calloc(1, sizeof(NoiseDetectorNlmsState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int ndnlms_destroy(void* state) { ndnlms_free((NoiseDetectorNlmsState*)state); return ORPHEUS_OK; }

static int ndnlms_prepare(void* state, const OrpheusConfig* config) {
    NoiseDetectorNlmsState* s = (NoiseDetectorNlmsState*)state;
    ndnlms_free(s);
    memset(s, 0, sizeof(*s));
    s->channels = config->channels > 0 ? (config->channels < NDNLMS_MAX_CH ? config->channels : NDNLMS_MAX_CH) : 2;
    s->filter_length = (uint32_t)read_int_param(config, "filter_length", 64);
    if (s->filter_length < 1) s->filter_length = 64;
    s->mu = read_float_param(config, "step_size", 0.1f);
    if (s->mu > 1.0f) s->mu = 1.0f;
    s->leak = read_float_param(config, "leakage", 1.0f);
    if (s->leak < 0.0f) s->leak = 0.0f; if (s->leak > 1.0f) s->leak = 1.0f;
    s->eps = read_float_param(config, "eps", 1e-6f);
    s->time_thres = read_float_param(config, "time_thres", 0.02f);
    s->frame_thres_db = read_float_param(config, "frame_thres_db", -25.0f);

    size_t ch = s->channels, L = s->filter_length;
    s->w   = (float*)calloc(ch * L, sizeof(float));
    s->x   = (float*)calloc(ch * L, sizeof(float));
    s->pos = (uint32_t*)calloc(ch, sizeof(uint32_t));
    if (!s->w || !s->x || !s->pos) { ndnlms_free(s); return ORPHEUS_ERR_OUT_OF_MEMORY; }
    return ORPHEUS_OK;
}

static int ndnlms_reset(void* state) {
    NoiseDetectorNlmsState* s = (NoiseDetectorNlmsState*)state;
    size_t ch = s->channels, L = s->filter_length;
    if (s->w) memset(s->w, 0, ch * L * sizeof(float));
    if (s->x) memset(s->x, 0, ch * L * sizeof(float));
    if (s->pos) memset(s->pos, 0, ch * sizeof(uint32_t));
    s->total_frames = 0; s->noise_frames = 0;
    s->total_samples = 0; s->noisy_samples = 0; s->clicks = 0;
    s->residue_db = 0.0f; s->echo_return_loss_db = 0.0f; s->residue_pk = 0.0f;
    s->erle_frames = 0; s->acc_erin = 0.0; s->acc_eres = 0.0;
    return ORPHEUS_OK;
}

static int ndnlms_process(void* state, const OrpheusProcessContext* ctx) {
    NoiseDetectorNlmsState* s = (NoiseDetectorNlmsState*)state;
    if (ctx->input_count < 2 || !ctx->inputs[0] || !ctx->inputs[1]) return ORPHEUS_ERR_INVALID_ARG;
    if (ctx->output_count < 1 || !ctx->outputs[0]) return ORPHEUS_ERR_INVALID_ARG;
    const float* ref = (const float*)ctx->inputs[0]->data;   /* reference / clean input */
    const float* in  = (const float*)ctx->inputs[1]->data;   /* under test */
    float* out = (float*)ctx->outputs[0]->data;
    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    uint32_t L = s->filter_length;

    /* passthrough under-test signal */
    memcpy(out, in, (size_t)frames * ch * sizeof(float));
    if (ctx->outputs[0]->frame_capacity >= frames) ctx->outputs[0]->frame_count = frames;

    double acc_res = 0.0, acc_in = 0.0;
    uint64_t noisy = 0, clicks = 0;
    float pk = 0.0f;

    for (uint32_t c = 0; c < ch; ++c) {
        float* wc = s->w + (size_t)c * L;
        float* xc = s->x + (size_t)c * L;
        uint32_t p = s->pos[c];
        for (uint32_t n = 0; n < frames; ++n) {
            float xv = ref[n * ch + c];
            xc[p] = xv;
            float y = 0.0f, norm = 0.0f;
            for (uint32_t i = 0; i < L; ++i) {
                uint32_t idx = (p + L - i) % L;
                float xk = xc[idx];
                y += wc[i] * xk;
                norm += xk * xk;
            }
            float d = in[n * ch + c];
            float e = d - y;                 /* residual after removing learned linear path */
            float r = fabsf(e);
            acc_res += (double)e * e;
            acc_in  += (double)d * d;
            if (r > s->time_thres) noisy++;
            if (r > 0.2f) clicks++;
            if (r > pk) pk = r;
            /* NLMS update on the residual (best linear-fit path) */
            float scale = s->mu * e / (norm + s->eps);
            for (uint32_t i = 0; i < L; ++i) {
                uint32_t idx = (p + L - i) % L;
                wc[i] = s->leak * wc[i] + scale * xc[idx];
            }
            p = (p + 1) % L;
        }
        s->pos[c] = p;
    }

    s->total_samples += (uint64_t)frames * ch;
    s->noisy_samples += noisy;
    s->clicks += (uint32_t)clicks;
    if (pk > s->residue_pk) s->residue_pk = pk;

    /* residual energy budget (dB): how much of the measured signal is NOT explained by the linear path */
    double res_db = 10.0 * log10((acc_res + 1e-12) / (acc_in + 1e-12));
    s->residue_db = (float)res_db;

    /* frame flagged as noisy if bounded residual budget exceeded */
    if (res_db > s->frame_thres_db) s->noise_frames++;
    s->total_frames++;

    /* ERLE proxy: ratio of measured energy to residual energy */
    s->acc_erin += acc_in; s->acc_eres += acc_res; s->erle_frames++;
    float erle = 10.0f * log10f((float)((s->acc_erin + 1e-12) / (s->acc_eres + 1e-12)));
    s->echo_return_loss_db = erle;  /* larger = cleaner */

    s->noise_ratio = s->total_frames ? (float)((double)s->noise_frames / (double)s->total_frames) : 0.0f;

    {
        char* p = s->json_detail; size_t rem = sizeof(s->json_detail);
        int len = snprintf(p, rem,
            "{\"residue_db\":%.2f,\"erle_db\":%.2f,\"noise_frames\":%llu,\"total_frames\":%llu,"
            "\"noisy_samples\":%llu,\"total_samples\":%llu,\"clicks\":%u,\"residue_pk\":%.4f}",
            (double)s->residue_db, (double)s->echo_return_loss_db,
            (unsigned long long)s->noise_frames, (unsigned long long)s->total_frames,
            (unsigned long long)s->noisy_samples, (unsigned long long)s->total_samples,
            s->clicks, (double)s->residue_pk);
        (void)len;
    }
    return ORPHEUS_OK;
}

static int ndnlms_get_parameter(void* state, const char* id, OrpheusValue* v) {
    NoiseDetectorNlmsState* s = (NoiseDetectorNlmsState*)state;
    if (!strcmp(id, "channels")) { v->type = ORPHEUS_VALUE_INT; v->value.i32 = (int32_t)s->channels; return ORPHEUS_OK; }
    if (!strcmp(id, "filter_length")) { v->type = ORPHEUS_VALUE_INT; v->value.i32 = (int32_t)s->filter_length; return ORPHEUS_OK; }
    if (!strcmp(id, "step_size")) { v->type = ORPHEUS_VALUE_FLOAT; v->value.f32 = s->mu; return ORPHEUS_OK; }
    if (!strcmp(id, "leakage")) { v->type = ORPHEUS_VALUE_FLOAT; v->value.f32 = s->leak; return ORPHEUS_OK; }
    if (!strcmp(id, "residue_db")) { v->type = ORPHEUS_VALUE_FLOAT; v->value.f32 = s->residue_db; return ORPHEUS_OK; }
    if (!strcmp(id, "erle_db")) { v->type = ORPHEUS_VALUE_FLOAT; v->value.f32 = s->echo_return_loss_db; return ORPHEUS_OK; }
    if (!strcmp(id, "noise_frames")) { v->type = ORPHEUS_VALUE_INT; v->value.i32 = (int32_t)(s->noise_frames > 0x7fffffff ? 0x7fffffff : s->noise_frames); return ORPHEUS_OK; }
    {
        float ratio = s->total_frames ? (float)((double)s->noise_frames / (double)s->total_frames) : 0.0f;
        if (!strcmp(id, "noise_ratio")) { v->type = ORPHEUS_VALUE_FLOAT; v->value.f32 = ratio; return ORPHEUS_OK; }
    }
    if (!strcmp(id, "clicks")) { v->type = ORPHEUS_VALUE_INT; v->value.i32 = (int32_t)(s->clicks > 0x7fffffff ? 0x7fffffff : s->clicks); return ORPHEUS_OK; }
    if (!strcmp(id, "residue_pk")) { v->type = ORPHEUS_VALUE_FLOAT; v->value.f32 = s->residue_pk; return ORPHEUS_OK; }
    if (!strcmp(id, "detail")) { v->type = ORPHEUS_VALUE_STRING; v->value.str = s->json_detail; return ORPHEUS_OK; }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int ndnlms_set_parameter(void* state, const char* id, const OrpheusValue* v) {
    (void)state; (void)id; (void)v;
    return ORPHEUS_ERR_UNSUPPORTED;
}

static int ndnlms_register_slots(void* state, const OrpheusRegistry* reg) {
    NoiseDetectorNlmsState* s = (NoiseDetectorNlmsState*)state;
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "\u901a\u9053\u6570", ORPHEUS_VALUE_INT,
        .min_i32=1, .max_i32=32, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
        .flags=ORPHEUS_SLOT_PERSISTENT|ORPHEUS_SLOT_READBACK|ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, filter_length, ORPHEUS_SLOT_SETTING, "filter_length", "\u6ee4\u6ce2\u5668\u957f\u5ea6", ORPHEUS_VALUE_INT,
        .min_i32=1, .max_i32=4096, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
        .flags=ORPHEUS_SLOT_PERSISTENT|ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, mu, ORPHEUS_SLOT_SETTING, "step_size", "\u6b65\u957f", ORPHEUS_VALUE_FLOAT,
        .min_f32=0.0f, .max_f32=1.0f, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
        .flags=ORPHEUS_SLOT_PERSISTENT|ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, eps, ORPHEUS_SLOT_SETTING, "eps", "\u6b63\u5219\u5316", ORPHEUS_VALUE_FLOAT,
        .min_f32=1e-12f, .max_f32=1.0f, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
        .flags=ORPHEUS_SLOT_PERSISTENT|ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, residue_db, ORPHEUS_SLOT_PROBE, "residue_db", "\u6b8b\u5dee\u80fd\u91cf(dB)", ORPHEUS_VALUE_FLOAT,
        .flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, echo_return_loss_db, ORPHEUS_SLOT_PROBE, "erle_db", "ERLE(dB)", ORPHEUS_VALUE_FLOAT,
        .flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, noise_frames, ORPHEUS_SLOT_PROBE, "noise_frames", "\u566a\u58f0\u5e27\u6570", ORPHEUS_VALUE_INT,
        .flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, noise_ratio, ORPHEUS_SLOT_PROBE, "noise_ratio", "\u566a\u58f0\u5360\u6bd4", ORPHEUS_VALUE_FLOAT,
        .flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, clicks, ORPHEUS_SLOT_PROBE, "clicks", "\u7a81\u523a\u8ba1\u6570", ORPHEUS_VALUE_INT,
        .flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, residue_pk, ORPHEUS_SLOT_PROBE, "residue_pk", "\u6b8b\u5dee\u5cf0\u503c", ORPHEUS_VALUE_FLOAT,
        .flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, json_detail, ORPHEUS_SLOT_PROBE, "detail", "\u660e\u7ec6", ORPHEUS_VALUE_STRING,
        .flags=ORPHEUS_SLOT_READBACK);
    return ORPHEUS_OK;
}

static const OrpheusParameter ndnlms_params[] = {
    { .id="channels", .name="\u901a\u9053\u6570", .type=ORPHEUS_VALUE_INT,
      .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=2}, .min_i32=1,.max_i32=32,
      .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true,.affects_signature=true },
    { .id="filter_length", .name="\u6ee4\u6ce2\u5668\u957f\u5ea6", .type=ORPHEUS_VALUE_INT,
      .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=64}, .min_i32=1,.max_i32=4096,
      .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true,.affects_signature=false },
    { .id="step_size", .name="\u6b65\u957f", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=0.1f}, .min_f32=0.0f,.max_f32=1.0f,
      .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true,.affects_signature=false },
    { .id="leakage", .name="\u6cc4\u6f0f\u56e0\u5b50", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=1.0f}, .min_f32=0.0f,.max_f32=1.0f,
      .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true,.affects_signature=false },
    { .id="eps", .name="\u6b63\u5219\u5316", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=1e-6f}, .min_f32=1e-12f,.max_f32=1.0f,
      .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true,.affects_signature=false },
    { .id="time_thres", .name="\u65f6\u57df\u6b8b\u5dee\u9608\u503c", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=0.02f}, .min_f32=0.0f,.max_f32=1.0f,
      .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true,.affects_signature=false },
    { .id="frame_thres_db", .name="\u5e27\u6b8b\u5dee\u9884\u7b97(dB)", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=-25.0f}, .min_f32=-120.0f,.max_f32=0.0f,
      .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true,.affects_signature=false },
    { .id="residue_db", .name="\u6b8b\u5dee\u80fd\u91cf(dB)", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=0.0f},
      .update_policy=ORPHEUS_UPDATE_IMMEDIATE,.readback=true,.persistent=false,.affects_signature=false },
    { .id="erle_db", .name="ERLE(dB)", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=0.0f},
      .update_policy=ORPHEUS_UPDATE_IMMEDIATE,.readback=true,.persistent=false,.affects_signature=false },
    { .id="noise_frames", .name="\u566a\u58f0\u5e27\u6570", .type=ORPHEUS_VALUE_INT,
      .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=0},
      .update_policy=ORPHEUS_UPDATE_IMMEDIATE,.readback=true,.persistent=false,.affects_signature=false },
    { .id="noise_ratio", .name="\u566a\u58f0\u5360\u6bd4", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=0.0f},
      .update_policy=ORPHEUS_UPDATE_IMMEDIATE,.readback=true,.persistent=false,.affects_signature=false },
    { .id="clicks", .name="\u7a81\u523a\u8ba1\u6570", .type=ORPHEUS_VALUE_INT,
      .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=0},
      .update_policy=ORPHEUS_UPDATE_IMMEDIATE,.readback=true,.persistent=false,.affects_signature=false },
    { .id="residue_pk", .name="\u6b8b\u5dee\u5cf0\u503c", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=0.0f},
      .update_policy=ORPHEUS_UPDATE_IMMEDIATE,.readback=true,.persistent=false,.affects_signature=false },
    { .id="detail", .name="\u660e\u7ec6", .type=ORPHEUS_VALUE_STRING,
      .default_value={.type=ORPHEUS_VALUE_STRING,.value.str="{}"},
      .update_policy=ORPHEUS_UPDATE_IMMEDIATE,.readback=true,.persistent=false,.affects_signature=false }
};
static const OrpheusPort ndnlms_ports[] = {
    { .id="ref", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32,
      .channels=0,.sample_rate=0,.block_size=0,.is_variable=true,.channels_param="channels" },
    { .id="in", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32,
      .channels=0,.sample_rate=0,.block_size=0,.is_variable=true,.channels_param="channels" },
    { .id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32,
      .channels=0,.sample_rate=0,.block_size=0,.is_variable=true,.channels_param="channels" }
};
static const OrpheusComponentDescriptor ndnlms_descriptor = {
    .id="orpheus.builtin.noise_detector_nlms", .version="1.0.0", .abi_version=ORPHEUS_ABI_VERSION,
    .ports=ndnlms_ports, .port_count=3, .params=ndnlms_params, .param_count=13,
    .state_size=sizeof(NoiseDetectorNlmsState), .scratch_size=0, .alignment=8,
    .latency_samples=0, .realtime_safe=true, .supports_inplace=false
};
static const OrpheusComponentDescriptor* ndnlms_get_descriptor(void){ return &ndnlms_descriptor; }

static const OrpheusComponentInterface ndnlms_interface = {
    .get_descriptor=ndnlms_get_descriptor,.create=ndnlms_create,.destroy=ndnlms_destroy,
    .prepare=ndnlms_prepare,.reset=ndnlms_reset,.process=ndnlms_process,
    .set_parameter=ndnlms_set_parameter,.get_parameter=ndnlms_get_parameter,
    .get_state_value=NULL,.register_slots=ndnlms_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void){ return &ndnlms_interface; }
