#include "orpheus_sweep_record.h"

#include <math.h>
#include <stdio.h>
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

static bool read_bool(const OrpheusConfig* config, const char* id, bool fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_BOOL) return config->param_values[i].value.b;
            if (config->param_values[i].type == ORPHEUS_VALUE_INT) return config->param_values[i].value.i32 != 0;
        }
    }
    return fallback;
}

static const OrpheusParameter params[] = {
    { .id = "start_freq", .name = "Start Freq", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 20.0f },
      .min_f32 = 1.0f, .max_f32 = 20000.0f, .unit = "Hz",
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED, .persistent = true },
    { .id = "end_freq", .name = "End Freq", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 20000.0f },
      .min_f32 = 1.0f, .max_f32 = 20000.0f, .unit = "Hz",
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED, .persistent = true },
    { .id = "duration_s", .name = "Duration", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 5.0f },
      .min_f32 = 0.1f, .max_f32 = 300.0f, .unit = "s",
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED, .persistent = true },
    { .id = "log_scale", .name = "Log Sweep", .type = ORPHEUS_VALUE_BOOL,
      .default_value = { .type = ORPHEUS_VALUE_BOOL, .value.b = true },
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED, .persistent = true },
    { .id = "bins", .name = "Bins", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 64 },
      .min_i32 = 8, .max_i32 = 256,
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED, .persistent = true },
    { .id = "channels", .name = "Channels", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 1 },
      .min_i32 = 1, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true }
};

static const OrpheusPort ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor desc = {
    .id = "orpheus.builtin.sweep_record", .version = "1.0.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = ports, .port_count = 2, .params = params, .param_count = 6,
    .state_size = sizeof(SweepRecordState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = true
};

static const OrpheusComponentDescriptor* get_desc(void) { return &desc; }

static int create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(SweepRecordState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int destroy(void* state) { (void)state; return ORPHEUS_OK; } /* v2：内存由 Runtime 统一管理 */

/* 与 sweep_gen 相同的频率公式：log: f0*(f1/f0)^(t/dur)；linear: f0+(f1-f0)*(t/dur) */
static double sweep_freq(const SweepRecordState* s, double tt) {
    double dur = s->duration_s > 0.0 ? (double)s->duration_s : 1.0;
    double f0 = s->start_freq, f1 = s->end_freq;
    if (f0 <= 0.0) f0 = 1.0;
    if (s->log_scale) {
        return f0 * pow(f1 / f0, tt / dur);
    }
    return f0 + (f1 - f0) * (tt / dur);
}

static int prepare(void* state, const OrpheusConfig* config) {
    SweepRecordState* s = (SweepRecordState*)state;
    s->channels = config->channels > 0 ? config->channels : 1;
    s->start_freq = read_float(config, "start_freq", 20.0f);
    s->end_freq = read_float(config, "end_freq", 20000.0f);
    s->duration_s = read_float(config, "duration_s", 5.0f);
    s->log_scale = read_bool(config, "log_scale", true);
    s->bins = (uint32_t)read_float(config, "bins", 64.0f);
    if (s->bins < 8) s->bins = 8;
    if (s->bins > SWEEP_RECORD_MAX_BINS) s->bins = SWEEP_RECORD_MAX_BINS;
    s->t = 0.0;
    s->total_frames = 0;
    s->duration_frames = (uint64_t)((double)s->duration_s * (double)config->sample_rate);
    if (s->duration_frames < 1) s->duration_frames = 1;
    s->done = false;
    s->progress = 0.0f;
    memset(s->acc, 0, sizeof(s->acc));
    memset(s->count, 0, sizeof(s->count));
    memset(s->mag, 0, sizeof(s->mag));
    /* 预计算各箱中心频率 */
    for (uint32_t i = 0; i < s->bins; ++i) {
        double u = (double)i / (double)(s->bins - 1);
        s->freq[i] = s->log_scale
            ? (float)((double)s->start_freq * pow((double)s->end_freq / (double)s->start_freq, u))
            : s->start_freq + (s->end_freq - s->start_freq) * (float)u;
    }
    s->json[0] = '\0';
    return ORPHEUS_OK;
}
static int reset(void* state) {
    SweepRecordState* s = (SweepRecordState*)state;
    s->t = 0.0;
    s->total_frames = 0;
    s->done = false;
    s->progress = 0.0f;
    memset(s->acc, 0, sizeof(s->acc));
    memset(s->count, 0, sizeof(s->count));
    memset(s->mag, 0, sizeof(s->mag));
    s->json[0] = '\0';
    return ORPHEUS_OK;
}

static void sweep_finalize(SweepRecordState* s) {
    for (uint32_t i = 0; i < s->bins; ++i) {
        s->mag[i] = s->count[i] > 0 ? sqrtf(s->acc[i] / (float)s->count[i]) : 0.0f;
    }
    s->done = true;
    s->progress = 1.0f;
}

static int process(void* state, const OrpheusProcessContext* ctx) {
    SweepRecordState* s = (SweepRecordState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (in == NULL || out == NULL) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;
    for (uint32_t i = 0; i < frames * ch; ++i) out_data[i] = in_data[i];  /* 直通 */
    out->frame_count = frames;

    if (s->done || s->duration_s <= 0.0f) return ORPHEUS_OK;

    /* 当前块起始时刻对应的扫频频率 → 分箱 */
    double tt = s->t;
    double dur = (double)s->duration_s;
    double f = sweep_freq(s, tt >= dur ? dur : tt);
    double f0 = s->start_freq, f1 = s->end_freq;
    double u = s->log_scale
        ? (log(f / f0) / log(f1 / f0))
        : ((f - f0) / (f1 - f0));
    if (u < 0.0) u = 0.0;
    if (u > 1.0) u = 1.0;
    int bin = (int)(u * (double)(s->bins - 1) + 0.5);
    if (bin < 0) bin = 0;
    if (bin >= (int)s->bins) bin = (int)s->bins - 1;

    double sum = 0.0;
    for (uint32_t i = 0; i < frames; ++i) {
        float x = in_data[(size_t)i * ch];
        sum += (double)x * (double)x;
    }
    s->acc[bin] += (float)sum;
    s->count[bin] += frames;
    s->t += (double)frames / (double)ctx->sample_rate;
    s->total_frames += frames;
    if (s->total_frames >= s->duration_frames) {
        sweep_finalize(s);
    } else {
        s->progress = (float)((double)s->total_frames / (double)s->duration_frames);
    }
    return ORPHEUS_OK;
}

static int set_param(void* state, const char* id, const OrpheusValue* v) {
    (void)state; (void)id; (void)v;
    return ORPHEUS_ERR_UNSUPPORTED;  /* 全部 restart_required */
}
static int get_param(void* state, const char* id, OrpheusValue* v) {
    SweepRecordState* s = (SweepRecordState*)state;
    if (strcmp(id, "channels") == 0) {
        v->type = ORPHEUS_VALUE_INT;
        v->value.i32 = (int32_t)s->channels;
        return ORPHEUS_OK;
    }
    if (strcmp(id, "progress") == 0) {
        v->type = ORPHEUS_VALUE_FLOAT;
        v->value.f32 = s->progress;
        return ORPHEUS_OK;
    }
    if (strcmp(id, "sweep") == 0) {
        /* 非实时线程：频率/幅度数组 + 完成标志 + 进度，编码为 JSON 对象 */
        char* p = s->json;
        size_t rem = sizeof(s->json);
        int n = snprintf(p, rem, "{\"freq\":[");
        if (n < 0) return ORPHEUS_ERR_PROCESSING;
        p += n; rem -= (size_t)n;
        for (uint32_t i = 0; i < s->bins && rem > 16; ++i) {
            n = snprintf(p, rem, i ? ",%.4g" : "%.4g", s->freq[i]);
            if (n < 0 || (size_t)n >= rem) break;
            p += n; rem -= (size_t)n;
        }
        if (rem > 2) { n = snprintf(p, rem, "],\"mag\":["); p += n; rem -= (size_t)n; }
        for (uint32_t i = 0; i < s->bins && rem > 16; ++i) {
            n = snprintf(p, rem, i ? ",%.6g" : "%.6g", s->mag[i]);
            if (n < 0 || (size_t)n >= rem) break;
            p += n; rem -= (size_t)n;
        }
        if (rem > 2) {
            n = snprintf(p, rem, "],\"done\":%s,\"progress\":%.4f}",
                         s->done ? "true" : "false", s->progress);
        }
        v->type = ORPHEUS_VALUE_STRING;
        v->value.str = s->json;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int register_slots(void* state, const OrpheusRegistry* reg) {
    SweepRecordState* s = (SweepRecordState*)state;
    ORPHEUS_REG_SLOT(reg, s, start_freq, ORPHEUS_SLOT_SETTING, "start_freq", "起始频率",
                     ORPHEUS_VALUE_FLOAT, .min_f32=1.0f, .max_f32=20000.0f, .unit="Hz",
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT);
    ORPHEUS_REG_SLOT(reg, s, end_freq, ORPHEUS_SLOT_SETTING, "end_freq", "结束频率",
                     ORPHEUS_VALUE_FLOAT, .min_f32=1.0f, .max_f32=20000.0f, .unit="Hz",
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT);
    ORPHEUS_REG_SLOT(reg, s, duration_s, ORPHEUS_SLOT_SETTING, "duration_s", "扫频时长",
                     ORPHEUS_VALUE_FLOAT, .min_f32=0.1f, .max_f32=300.0f, .unit="s",
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT);
    ORPHEUS_REG_SLOT(reg, s, log_scale, ORPHEUS_SLOT_SETTING, "log_scale", "对数扫频",
                     ORPHEUS_VALUE_BOOL, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT);
    ORPHEUS_REG_SLOT(reg, s, bins, ORPHEUS_SLOT_SETTING, "bins", "记录点数",
                     ORPHEUS_VALUE_INT, .min_i32=8, .max_i32=256,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT);
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, json, ORPHEUS_SLOT_PROBE, "sweep", "扫频曲线",
                     ORPHEUS_VALUE_STRING, .flags=ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, progress, ORPHEUS_SLOT_PROBE, "progress", "扫频进度",
                     ORPHEUS_VALUE_FLOAT, .flags=ORPHEUS_SLOT_READBACK);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface iface = {
    get_desc, create, destroy, prepare, reset, process, set_param, get_param, NULL, register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &iface; }
