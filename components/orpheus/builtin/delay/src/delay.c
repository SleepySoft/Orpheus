#include "orpheus_delay.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static const OrpheusParameter params[] = {
    { .id = "delay_ms", .name = "Delay Time", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 100.0f },
      .min_f32 = 0.0f, .max_f32 = 5000.0f, .unit = "ms",
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED, .readback = true, .persistent = true },
    { .id = "mix", .name = "Mix", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.5f },
      .min_f32 = 0.0f, .max_f32 = 1.0f,
      .update_policy = ORPHEUS_UPDATE_SMOOTHED, .readback = true, .persistent = true },
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
    "orpheus.builtin.delay", "1.0.1", ORPHEUS_ABI_VERSION,
    ports, 2, params, 3, sizeof(DelayState), 0, 8, 0, true, false
};

static const OrpheusComponentDescriptor* get_desc(void) { return &desc; }
static int create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) { *state = config->state_block; return ORPHEUS_OK; }
    *state = calloc(1, sizeof(DelayState)); return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int destroy(void* state) {
    DelayState* s = (DelayState*)state;
    if (s->buffer) free(s->buffer);   /* 延迟线是组件内部资源，仍需释放 */
    return ORPHEUS_OK;                /* v2：状态块本身由 Runtime 统一管理 */
}
static int prepare(void* state, const OrpheusConfig* config) {
    DelayState* s = (DelayState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    float delay_ms = 100.0f;
    float mix = 0.5f;
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (!config->param_ids[i]) continue;
        if (strcmp(config->param_ids[i], "delay_ms") == 0 && config->param_values[i].type == ORPHEUS_VALUE_FLOAT)
            delay_ms = config->param_values[i].value.f32;
        else if (strcmp(config->param_ids[i], "mix") == 0 && config->param_values[i].type == ORPHEUS_VALUE_FLOAT)
            mix = config->param_values[i].value.f32;
    }
    if (mix < 0.0f) mix = 0.0f;
    if (mix > 1.0f) mix = 1.0f;
    s->delay_samples = (uint32_t)((delay_ms / 1000.0f) * config->sample_rate + 0.5f);
    s->delay_ms = delay_ms;
    s->mix = mix;
    /* 容量必须是通道数的整数倍：write_pos 按 ch 递增取模，否则通道错位会通道串扰，且可能越界读写（兹啦噪声/崩溃）。 */
    uint32_t block_size = config->block_size > 0 ? config->block_size : 1024;
    s->capacity = (s->delay_samples + block_size + 1) * s->channels;
    free(s->buffer);            /* 幂等 prepare：重入先释放旧缓冲，防泄漏 */
    s->buffer = NULL;
    s->buffer = (float*)calloc(s->capacity, sizeof(float));
    if (!s->buffer) return ORPHEUS_ERR_OUT_OF_MEMORY;
    s->write_pos = 0;
    s->mix_smoothed = mix;
    s->mix_coeff = 1.0f;
    if (config->sample_rate > 0) {
        /* mix 为 smoothed：约 5ms 一阶平滑，避免实时调参数产生尖声 */
        float tau = 0.005f;
        s->mix_coeff = 1.0f - expf(-1.0f / (tau * (float)config->sample_rate));
        if (s->mix_coeff > 1.0f) s->mix_coeff = 1.0f;
    }
    return ORPHEUS_OK;
}
static int reset(void* state) {
    DelayState* s = (DelayState*)state;
    if (s->buffer) memset(s->buffer, 0, s->capacity * sizeof(float));
    s->write_pos = 0;
    if (s->mix_coeff > 0.0f) s->mix_smoothed = s->mix;
    return ORPHEUS_OK;
}
static int process(void* state, const OrpheusProcessContext* ctx) {
    DelayState* s = (DelayState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (!in || !out) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;
    for (uint32_t n = 0; n < frames; ++n) {
        for (uint32_t c = 0; c < ch; ++c) {
            float x = in_data[n * ch + c];
            /* 先写后读：delay_samples==0 时读位置==写位置，先写入保证读到的是当前样本（真直通），
               否则会读到一整圈之前的旧数据（0 延迟出现梳状杂音）。delay>0 时读写位置恒不重叠。 */
            s->buffer[s->write_pos + c] = x;
            uint32_t idx = (s->write_pos + s->capacity - s->delay_samples * ch) % s->capacity;
            float delayed = s->buffer[idx + c];
            /* mix 为 smoothed：一阶平滑，避免实时调参数产生尖声；m=平滑后 wet 占比 */
            s->mix_smoothed += s->mix_coeff * (s->mix - s->mix_smoothed);
            float m = s->mix_smoothed;
            /* out = (1-m)*x + m*delayed，凸组合不削波 */
            out_data[n * ch + c] = (1.0f - m) * x + m * delayed;
        }
        s->write_pos = (s->write_pos + ch) % s->capacity;
    }
    out->frame_count = frames;
    return ORPHEUS_OK;
}
static int set_param(void* state, const char* id, const OrpheusValue* v) {
    DelayState* s = (DelayState*)state;
    if (strcmp(id, "mix") == 0 && v->type == ORPHEUS_VALUE_FLOAT) { s->mix = v->value.f32; return ORPHEUS_OK; }
    return ORPHEUS_ERR_UNSUPPORTED;
}
static int get_param(void* state, const char* id, OrpheusValue* v) {
    DelayState* s = (DelayState*)state;
    if (strcmp(id, "mix") == 0) { v->type = ORPHEUS_VALUE_FLOAT; v->value.f32 = s->mix; return ORPHEUS_OK; }
    if (strcmp(id, "channels") == 0) { v->type = ORPHEUS_VALUE_INT; v->value.i32 = (int32_t)s->channels; return ORPHEUS_OK; }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    DelayState* s = (DelayState*)state;
    ORPHEUS_REG_SLOT(reg, s, delay_ms, ORPHEUS_SLOT_SETTING, "delay_ms", "延迟时间",
                     ORPHEUS_VALUE_FLOAT, .min_f32=0.0f, .max_f32=5000.0f, .unit="ms",
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, mix, ORPHEUS_SLOT_SETTING, "mix", "混合比",
                     ORPHEUS_VALUE_FLOAT, .min_f32=0.0f, .max_f32=1.0f,
                     .update_policy=ORPHEUS_UPDATE_SMOOTHED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
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
