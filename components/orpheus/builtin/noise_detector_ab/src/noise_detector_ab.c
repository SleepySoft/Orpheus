#include "orpheus_noise_detector_ab.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define PI_F 3.14159265358979f

static uint32_t is_pow2(uint32_t v) { return v && (v & (v - 1)) == 0; }

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

/* radix-2 complex FFT, in place */
static void fft_radix2(float* re, float* im, uint32_t n) {
    for (uint32_t i = 1, j = 0; i < n; ++i) {
        uint32_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { float t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (uint32_t len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * PI_F / (float)len;
        float wr = cosf(ang), wi = sinf(ang);
        for (uint32_t i = 0; i < n; i += len) {
            float cwr = 1.0f, cwi = 0.0f;
            for (uint32_t k = 0; k < len / 2; ++k) {
                uint32_t a = i + k, b = i + k + len / 2;
                float tr = cwr * re[b] - cwi * im[b];
                float ti = cwr * im[b] + cwi * re[b];
                re[b] = re[a] - tr; im[b] = im[a] - ti;
                re[a] += tr;       im[a] += ti;
                float nwr = cwr * wr - cwi * wi;
                cwi = cwr * wi + cwi * wr;
                cwr = nwr;
            }
        }
    }
}

static void ndab_free(NoiseDetectorAbState* s) {
    free(s->sxx); free(s->syy); free(s->sxy_re); free(s->sxy_im);
    free(s->win); free(s->rea); free(s->ima);
    s->sxx = s->syy = s->sxy_re = s->sxy_im = s->win = s->rea = s->ima = NULL;
}

static int ndab_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) { *state = config->state_block; return ORPHEUS_OK; }
    *state = calloc(1, sizeof(NoiseDetectorAbState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int ndab_destroy(void* state) { ndab_free((NoiseDetectorAbState*)state); return ORPHEUS_OK; }

static int ndab_prepare(void* state, const OrpheusConfig* config) {
    NoiseDetectorAbState* s = (NoiseDetectorAbState*)state;
    ndab_free(s);
    memset(s, 0, sizeof(*s));
    s->channels = config->channels > 0 ? (config->channels < NDAB_MAX_CH ? config->channels : NDAB_MAX_CH) : 2;
    s->fft_size = (uint32_t)read_int_param(config, "fft_size", 0);
    if (s->fft_size == 0 || !is_pow2(s->fft_size)) {
        uint32_t b = config->block_size > 0 ? config->block_size : 128;
        s->fft_size = b;
        while (!is_pow2(s->fft_size) && s->fft_size > 2) s->fft_size >>= 1;
        if (s->fft_size < 2) s->fft_size = 2;
    }
    if (s->fft_size > NDAB_MAX_FFT) s->fft_size = NDAB_MAX_FFT;
    s->half = s->fft_size / 2;
    if (s->half < 1) s->half = 1;

    int sm = read_int_param(config, "smoothing", 16);
    s->alpha = 1.0f / (float)(sm > 0 ? sm : 16);
    s->noise_thresh_db = read_float_param(config, "threshold_db", -40.0f);
    s->time_thres = read_float_param(config, "time_thres", 0.02f);

    size_t ch = s->channels, h = s->half;
    s->sxx    = (float*)calloc(ch * h, sizeof(float));
    s->syy    = (float*)calloc(ch * h, sizeof(float));
    s->sxy_re = (float*)calloc(ch * h, sizeof(float));
    s->sxy_im = (float*)calloc(ch * h, sizeof(float));
    s->win    = (float*)malloc(s->fft_size * sizeof(float));
    s->rea    = (float*)malloc(s->fft_size * sizeof(float));
    s->ima    = (float*)malloc(s->fft_size * sizeof(float));
    if (!s->sxx || !s->syy || !s->sxy_re || !s->sxy_im || !s->win || !s->rea || !s->ima) {
        ndab_free(s); return ORPHEUS_ERR_OUT_OF_MEMORY;
    }
    for (uint32_t k = 0; k < s->fft_size; ++k)
        s->win[k] = 0.5f - 0.5f * cosf(2.0f * PI_F * (float)k / (float)(s->fft_size - 1));

    /* band edges in bins: lo [0, lo_end) mid [lo_end, mid_end) hi [mid_end, half) */
    s->lo_end = s->half / 4; if (s->lo_end < 1) s->lo_end = 1;
    s->mid_end = 3 * s->half / 4; if (s->mid_end <= s->lo_end) s->mid_end = s->lo_end + 1;
    return ORPHEUS_OK;
}

static int ndab_reset(void* state) {
    NoiseDetectorAbState* s = (NoiseDetectorAbState*)state;
    size_t n = (size_t)s->channels * s->half;
    if (s->sxx) memset(s->sxx, 0, n * sizeof(float));
    if (s->syy) memset(s->syy, 0, n * sizeof(float));
    if (s->sxy_re) memset(s->sxy_re, 0, n * sizeof(float));
    if (s->sxy_im) memset(s->sxy_im, 0, n * sizeof(float));
    s->total_frames = 0; s->noise_frames = 0;
    s->total_samples = 0; s->noisy_samples = 0; s->clicks = 0;
    return ORPHEUS_OK;
}

static int ndab_process(void* state, const OrpheusProcessContext* ctx) {
    NoiseDetectorAbState* s = (NoiseDetectorAbState*)state;
    if (ctx->input_count < 2 || !ctx->inputs[0] || !ctx->inputs[1]) return ORPHEUS_ERR_INVALID_ARG;
    if (ctx->output_count < 1 || !ctx->outputs[0]) return ORPHEUS_ERR_INVALID_ARG;
    const float* ref = (const float*)ctx->inputs[0]->data;
    const float* in  = (const float*)ctx->inputs[1]->data;
    float* out = (float*)ctx->outputs[0]->data;
    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    uint32_t fft = s->fft_size, h = s->half;

    /* passthrough: out = in (under test), byte-copy the valid region */
    memcpy(out, in, (size_t)frames * ch * sizeof(float));
    if (ctx->outputs[0]->frame_capacity >= frames) ctx->outputs[0]->frame_count = frames;

    uint32_t n_win = frames / fft; if (n_win < 1) n_win = 0;

    /* time-domain residual + click detection over this block */
    float block_thd = 0.0f;
    double acc_tnd = 0.0, acc_sy = 0.0;
    double acc_ratio = (double)s->time_thres;

    bool has_spectral = (n_win > 0);
    if (has_spectral) {
        for (uint32_t w = 0; w < n_win; ++w) {
            size_t off = (size_t)w * fft;
            double sum_tnd = 0.0, sum_sy = 0.0;
            for (uint32_t c = 0; c < ch; ++c) {
                for (uint32_t k = 0; k < fft; ++k) {
                    float xv = ref[(off + k) * ch + c] * s->win[k];
                    float yv = in [(off + k) * ch + c] * s->win[k];
                    s->rea[k] = xv; s->ima[k] = 0.0f;
                }
                fft_radix2(s->rea, s->ima, fft);
                /* store X for this window to build Sxy with Y */
                float xre[NDAB_MAX_FFT]; float xim[NDAB_MAX_FFT];
                memcpy(xre, s->rea, fft * sizeof(float));
                memcpy(xim, s->ima, fft * sizeof(float));
                for (uint32_t k = 0; k < fft; ++k) {
                    float yv = in[(off + k) * ch + c] * s->win[k];
                    s->rea[k] = yv; s->ima[k] = 0.0f;
                }
                fft_radix2(s->rea, s->ima, fft);
                float a = s->alpha;
                for (uint32_t k = 0; k < h; ++k) {
                    size_t ix = (size_t)c * h + k;
                    float xr = xre[k], xi = xim[k];
                    float yr = s->rea[k], yi = s->ima[k];
                    float sxx = xr*xr + xi*xi;
                    float syy = yr*yr + yi*yi;
                    float re = xr*yr + xi*yi;   /* Sxy = X conj(Y) real */
                    float im = xi*yr - xr*yi;   /* imag  = conj(Y)*X ... sign; use Sxy = X*conj(Y): a-ib' */
                    /* X * conj(Y) = (xr+xi i)(yr - yi i) = xr*yr + xi*yi + (xi*yr - xr*yi) i */
                    /* -> re = xr*yr + xi*yi ; im = xi*yr - xr*yi */
                    s->sxx[ix] += a * (sxx - s->sxx[ix]);
                    s->syy[ix] += a * (syy - s->syy[ix]);
                    s->sxy_re[ix] += a * (re - s->sxy_re[ix]);
                    s->sxy_im[ix] += a * (im - s->sxy_im[ix]);
                }
            }
            /* aggregate this window */
            for (uint32_t c = 0; c < ch; ++c) {
                for (uint32_t k = 1; k < h; ++k) {
                    size_t ix = (size_t)c * h + k;
                    float gxx = s->sxx[ix], gyy = s->syy[ix];
                    float gxr = s->sxy_re[ix], gxi = s->sxy_im[ix];
                    float mag2 = gxr*gxr + gxi*gxi;
                    float den = gxx * gyy;
                    float gam = den > 1e-12f ? mag2 / den : 0.0f;
                    if (gam > 1.0f) gam = 1.0f;
                    sum_sy += gyy;
                    sum_tnd += (1.0f - gam) * gyy;
                }
            }
            acc_sy += sum_sy; acc_tnd += sum_tnd;
        }
        if (acc_sy > 1e-12) {
            double r = acc_tnd / acc_sy;
            block_thd = 10.0f * log10f((float)(r > 1e-12 ? r : 1e-12));
        } else {
            block_thd = -120.0f;
        }
        s->thd_n_db = block_thd;
    }

    /* time-domain residual samples over full (non-windowed) block */
    {
        uint64_t noisy = 0, clicks = 0;
        double rms_ref = 0.0;
        for (uint32_t i = 0; i < (uint32_t)frames * ch; ++i) {
            float d = ref[i] - in[i];
            float ad = fabsf(d);
            if (ad > s->time_thres) noisy++;
            /* click: large positive excursion of residual relative to local */
            if (ad > 0.2f) clicks++;
        }
        for (uint32_t i = 0; i < (uint32_t)frames * ch; ++i) rms_ref += (double)ref[i]*ref[i];
        rms_ref = sqrt(rms_ref / ((uint32_t)frames * ch + 1));
        s->noisy_samples += noisy;
        s->total_samples += (uint64_t)frames * ch;
        s->clicks += (uint32_t)clicks;

        /* residue peak over block */
        float pk = 0.0f;
        for (uint32_t i = 0; i < (uint32_t)frames * ch; ++i) {
            float ad = fabsf(ref[i] - in[i]);
            if (ad > pk) pk = ad;
        }
        if (pk > s->residue_pk) s->residue_pk = pk;
    }

    /* frame-level threshold count */
    if (block_thd > s->noise_thresh_db && has_spectral) {
        s->noise_frames++;
    }
    s->total_frames += (uint32_t)(has_spectral ? n_win : 0);
    s->noise_ratio = s->total_frames ? (float)((double)s->noise_frames/(double)s->total_frames) : 0.0f;

    /* per-band coherence (latest smoothed) mean over channels */
    {
        double cl=0, cm=0, chh=0; uint32_t nl=0, nm=0, nh=0;
        for (uint32_t c = 0; c < ch; ++c) {
            for (uint32_t k = 1; k < h; ++k) {
                size_t ix=(size_t)c*h+k;
                float gxx=s->sxx[ix], gyy=s->syy[ix];
                float gxr=s->sxy_re[ix], gxi=s->sxy_im[ix];
                float mag2=gxr*gxr+gxi*gxi;
                float den=gxx*gyy;
                float gam = den>1e-12f? mag2/den : 0.0f; if(gam>1.0f)gam=1.0f;
                if(k<s->lo_end){cl+=gam;nl++;} else if(k<s->mid_end){cm+=gam;nm++;} else {chh+=gam;nh++;}
            }
        }
        s->coh_lo  = nl? (float)(cl/(double)nl):0.0f;
        s->coh_mid = nm? (float)(cm/(double)nm):0.0f;
        s->coh_hi  = nh? (float)(chh/(double)nh):0.0f;
    }

    /* JSON detail */
    {
        char* p = s->json_detail; size_t rem=sizeof(s->json_detail);
        int len = snprintf(p, rem,
            "{\"thd_n_db\":%.2f,\"coh_lo\":%.3f,\"coh_mid\":%.3f,\"coh_hi\":%.3f,"
            "\"noise_frames\":%llu,\"total_frames\":%llu,"
            "\"noisy_samples\":%llu,\"total_samples\":%llu,\"clicks\":%u,\"residue_pk\":%.4f}",
            (double)s->thd_n_db, (double)s->coh_lo, (double)s->coh_mid, (double)s->coh_hi,
            (unsigned long long)s->noise_frames, (unsigned long long)s->total_frames,
            (unsigned long long)s->noisy_samples, (unsigned long long)s->total_samples,
            s->clicks, (double)s->residue_pk);
        (void)len;
    }
    return ORPHEUS_OK;
}

static int ndab_set_parameter(void* state, const char* id, const OrpheusValue* v) {
    (void)state;(void)id;(void)v;
    return ORPHEUS_ERR_UNSUPPORTED;
}
static int ndab_get_parameter(void* state, const char* id, OrpheusValue* v) {
    NoiseDetectorAbState* s=(NoiseDetectorAbState*)state;
    if (!strcmp(id,"thd_n_db")){v->type=ORPHEUS_VALUE_FLOAT;v->value.f32=s->thd_n_db;return ORPHEUS_OK;}
    if (!strcmp(id,"noise_frames")){v->type=ORPHEUS_VALUE_INT;v->value.i32=(int32_t)(s->noise_frames>0x7fffffff?0x7fffffff:s->noise_frames);return ORPHEUS_OK;}
    {
        float ratio = s->total_frames? (float)((double)s->noise_frames/(double)s->total_frames):0.0f;
        if (!strcmp(id,"noise_ratio")){v->type=ORPHEUS_VALUE_FLOAT;v->value.f32=ratio;return ORPHEUS_OK;}
    }
    if (!strcmp(id,"clicks")){v->type=ORPHEUS_VALUE_INT;v->value.i32=(int32_t)(s->clicks>0x7fffffff?0x7fffffff:s->clicks);return ORPHEUS_OK;}
    if (!strcmp(id,"residue_pk")){v->type=ORPHEUS_VALUE_FLOAT;v->value.f32=s->residue_pk;return ORPHEUS_OK;}
    if (!strcmp(id,"detail")){v->type=ORPHEUS_VALUE_STRING;v->value.str=s->json_detail;return ORPHEUS_OK;}
    if (!strcmp(id,"channels")){v->type=ORPHEUS_VALUE_INT;v->value.i32=(int32_t)s->channels;return ORPHEUS_OK;}
    return ORPHEUS_ERR_NOT_FOUND;
}

static int ndab_register_slots(void* state, const OrpheusRegistry* reg) {
    NoiseDetectorAbState* s=(NoiseDetectorAbState*)state;
    ORPHEUS_REG_SLOT(reg,s,channels,ORPHEUS_SLOT_SETTING,"channels","通道数",ORPHEUS_VALUE_INT,
        .min_i32=1,.max_i32=32,.update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
        .flags=ORPHEUS_SLOT_PERSISTENT|ORPHEUS_SLOT_READBACK|ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg,s,alpha,ORPHEUS_SLOT_SETTING,"smoothing","平滑块数",ORPHEUS_VALUE_FLOAT,
        .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.flags=ORPHEUS_SLOT_PERSISTENT|ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg,s,noise_thresh_db,ORPHEUS_SLOT_SETTING,"threshold_db","失真噪声阈值",ORPHEUS_VALUE_FLOAT,
        .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.flags=ORPHEUS_SLOT_PERSISTENT|ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg,s,time_thres,ORPHEUS_SLOT_SETTING,"time_thres","时域残差阈值",ORPHEUS_VALUE_FLOAT,
        .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.flags=ORPHEUS_SLOT_PERSISTENT|ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg,s,thd_n_db,ORPHEUS_SLOT_PROBE,"thd_n_db","失真+噪声",ORPHEUS_VALUE_FLOAT,
        .flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg,s,noise_frames,ORPHEUS_SLOT_PROBE,"noise_frames","噪声帧数",ORPHEUS_VALUE_INT,
        .flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg,s,noise_ratio,ORPHEUS_SLOT_PROBE,"noise_ratio","噪声占比",ORPHEUS_VALUE_FLOAT,
        .flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg,s,clicks,ORPHEUS_SLOT_PROBE,"clicks","突刺计数",ORPHEUS_VALUE_INT,
        .flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg,s,residue_pk,ORPHEUS_SLOT_PROBE,"residue_pk","残差峰值",ORPHEUS_VALUE_FLOAT,
        .flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg,s,json_detail,ORPHEUS_SLOT_PROBE,"detail","频带明细",ORPHEUS_VALUE_STRING,
        .flags=ORPHEUS_SLOT_READBACK);
    return ORPHEUS_OK;
}

static const OrpheusParameter ndab_params[] = {
    { .id="channels", .name="通道数", .type=ORPHEUS_VALUE_INT,
      .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=2}, .min_i32=1,.max_i32=32,
      .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true,.affects_signature=true },
    { .id="smoothing", .name="平滑块数", .type=ORPHEUS_VALUE_INT,
      .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=16}, .min_i32=1,.max_i32=128,
      .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true,.affects_signature=false },
    { .id="threshold_db", .name="失真噪声阈值", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=-40.0f}, .min_f32=-120.0f,.max_f32=0.0f,
      .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true,.affects_signature=false },
    { .id="time_thres", .name="时域残差阈值", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=0.02f},
      .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,.readback=true,.persistent=true,.affects_signature=false },
    { .id="thd_n_db", .name="失真+噪声", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=0.0f},
      .update_policy=ORPHEUS_UPDATE_IMMEDIATE,.readback=true,.persistent=false,.affects_signature=false },
    { .id="noise_frames", .name="噪声帧数", .type=ORPHEUS_VALUE_INT,
      .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=0},
      .update_policy=ORPHEUS_UPDATE_IMMEDIATE,.readback=true,.persistent=false,.affects_signature=false },
    { .id="noise_ratio", .name="噪声占比", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=0.0f},
      .update_policy=ORPHEUS_UPDATE_IMMEDIATE,.readback=true,.persistent=false,.affects_signature=false },
    { .id="clicks", .name="突刺计数", .type=ORPHEUS_VALUE_INT,
      .default_value={.type=ORPHEUS_VALUE_INT,.value.i32=0},
      .update_policy=ORPHEUS_UPDATE_IMMEDIATE,.readback=true,.persistent=false,.affects_signature=false },
    { .id="residue_pk", .name="残差峰值", .type=ORPHEUS_VALUE_FLOAT,
      .default_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=0.0f},
      .update_policy=ORPHEUS_UPDATE_IMMEDIATE,.readback=true,.persistent=false,.affects_signature=false },
    { .id="detail", .name="频带明细", .type=ORPHEUS_VALUE_STRING,
      .default_value={.type=ORPHEUS_VALUE_STRING,.value.str="{}"},
      .update_policy=ORPHEUS_UPDATE_IMMEDIATE,.readback=true,.persistent=false,.affects_signature=false }
};
static const OrpheusPort ndab_ports[] = {
    { .id="ref", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32,
      .channels=0,.sample_rate=0,.block_size=0,.is_variable=true,.channels_param="channels" },
    { .id="in", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32,
      .channels=0,.sample_rate=0,.block_size=0,.is_variable=true,.channels_param="channels" },
    { .id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32,
      .channels=0,.sample_rate=0,.block_size=0,.is_variable=true,.channels_param="channels" }
};
static const OrpheusComponentDescriptor ndab_descriptor = {
    .id="orpheus.builtin.noise_detector_ab", .version="1.0.0", .abi_version=ORPHEUS_ABI_VERSION,
    .ports=ndab_ports, .port_count=3, .params=ndab_params, .param_count=10,
    .state_size=sizeof(NoiseDetectorAbState), .scratch_size=0, .alignment=8,
    .latency_samples=0, .realtime_safe=true, .supports_inplace=false
};
static const OrpheusComponentDescriptor* ndab_get_descriptor(void){ return &ndab_descriptor; }

static const OrpheusComponentInterface ndab_interface = {
    .get_descriptor=ndab_get_descriptor,.create=ndab_create,.destroy=ndab_destroy,
    .prepare=ndab_prepare,.reset=ndab_reset,.process=ndab_process,
    .set_parameter=ndab_set_parameter,.get_parameter=ndab_get_parameter,
    .get_state_value=NULL,.register_slots=ndab_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void){ return &ndab_interface; }
