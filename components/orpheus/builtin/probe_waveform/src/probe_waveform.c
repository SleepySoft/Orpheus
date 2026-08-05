#include "orpheus_probe_waveform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROBE_WAVEFORM_SAMPLES 1024 /* 环形缓冲容量（帧，取第 0 通道） */
#define PROBE_WAVEFORM_JSON_CAP 20480

typedef struct {
    uint32_t channels;
    uint32_t head; /* 下一个写入位置 */
    float buf[PROBE_WAVEFORM_SAMPLES];
    char json[PROBE_WAVEFORM_JSON_CAP]; /* get_param 非实时线程格式化用 */
} ProbeWaveformState;

static const OrpheusParameter params[] = {
    { .id = "channels", .name = "Channels", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "waveform", .name = "Waveform", .type = ORPHEUS_VALUE_STRING,
      .default_value = { .type = ORPHEUS_VALUE_STRING, .value.str = "" },
      .update_policy = ORPHEUS_UPDATE_IMMEDIATE,
      .readback = true, .persistent = false, .affects_signature = false }
};

static const OrpheusPort ports[] = {
    { "in", ORPHEUS_PORT_INPUT, ORPHEUS_PORT_AUDIO, ORPHEUS_FORMAT_F32, 0, 0, 0, true, "channels" },
    { "out", ORPHEUS_PORT_OUTPUT, ORPHEUS_PORT_AUDIO, ORPHEUS_FORMAT_F32, 0, 0, 0, true, "channels" }
};

static const OrpheusComponentDescriptor desc = {
    "orpheus.builtin.probe_waveform", "1.0.0", ORPHEUS_ABI_VERSION,
    ports, 2, params, 2, sizeof(ProbeWaveformState), 0, 8, 0, true, true
};

static const OrpheusComponentDescriptor* get_desc(void) { return &desc; }
static int create(void** state, const OrpheusConfig* config) {
    (void)config; *state = calloc(1, sizeof(ProbeWaveformState)); return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int destroy(void* state) { free(state); return ORPHEUS_OK; }
static int prepare(void* state, const OrpheusConfig* config) {
    ProbeWaveformState* s = (ProbeWaveformState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    s->head = 0;
    memset(s->buf, 0, sizeof(s->buf));
    s->json[0] = '\0';
    return ORPHEUS_OK;
}
static int reset(void* state) { (void)state; return ORPHEUS_OK; }
static int process(void* state, const OrpheusProcessContext* ctx) {
    ProbeWaveformState* s = (ProbeWaveformState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (!in) return ORPHEUS_ERR_INVALID_ARG;
    /* 实时路径：只做定长 memcpy/环形写入，禁止分配与 IO */
    uint32_t frames = ctx->frame_count;
    const float* in_data = (const float*)in->data;
    if (in_data && frames > 0) {
        uint32_t ch = s->channels > 0 ? s->channels : 1;
        for (uint32_t i = 0; i < frames; ++i) {
            s->buf[s->head] = in_data[i * ch]; /* 取第 0 通道 */
            s->head = (s->head + 1) % PROBE_WAVEFORM_SAMPLES;
        }
    }
    if (out) {
        float* out_data = (float*)out->data;
        if (out_data) memcpy(out_data, in_data, frames * s->channels * sizeof(float));
        out->frame_count = frames;
    }
    return ORPHEUS_OK;
}
static int set_param(void* state, const char* id, const OrpheusValue* v) { (void)state; (void)id; (void)v; return ORPHEUS_ERR_UNSUPPORTED; }
static int get_param(void* state, const char* id, OrpheusValue* v) {
    ProbeWaveformState* s = (ProbeWaveformState*)state;
    if (strcmp(id, "channels") == 0) { v->type = ORPHEUS_VALUE_INT; v->value.i32 = (int32_t)s->channels; return ORPHEUS_OK; }
    if (strcmp(id, "waveform") == 0) {
        /* 非实时线程（probe 上报）：把环形缓冲编码为 JSON 数组字符串。
           host 对 STRING readback 参数以 PROBE_JSON <node> <param> <json> 上报。 */
        char* p = s->json;
        size_t rem = sizeof(s->json);
        int n = snprintf(p, rem, "[");
        if (n < 0) return ORPHEUS_ERR_PROCESSING;
        p += n; rem -= (size_t)n;
        for (uint32_t i = 0; i < PROBE_WAVEFORM_SAMPLES && rem > 16; ++i) {
            float x = s->buf[(s->head + i) % PROBE_WAVEFORM_SAMPLES];
            n = snprintf(p, rem, i ? ",%.6g" : "%.6g", x);
            if (n < 0 || (size_t)n >= rem) break;
            p += n; rem -= (size_t)n;
        }
        if (rem > 2) snprintf(p, rem, "]");
        v->type = ORPHEUS_VALUE_STRING;
        v->value.str = s->json;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static const OrpheusComponentInterface iface = {
    get_desc, create, destroy, prepare, reset, process, set_param, get_param, NULL
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &iface; }
