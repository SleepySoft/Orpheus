#include "orpheus_biquad_bank.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI_F 3.14159265358979f

static float read_float(const OrpheusConfig* config, const char* id, float fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_FLOAT) return config->param_values[i].value.f32;
            if (config->param_values[i].type == ORPHEUS_VALUE_INT) return (float)config->param_values[i].value.i32;
        }
    }
    return fallback;
}

/* RBJ peaking 系数（与 biquad 组件同式；后续可抽到 orpheus.dsp.common 共享库）。 */
static void bank_peaking_coeffs(float fc, float q, float gain_db, float sr,
                                float* b0, float* b1, float* b2, float* a1, float* a2) {
    float A = powf(10.0f, gain_db / 40.0f);
    float w0 = 2.0f * PI_F * fc / sr;
    float cosw0 = cosf(w0);
    float sinw0 = sinf(w0);
    float alpha = sinw0 / (2.0f * q);
    *b0 = 1.0f + alpha * A;
    *b1 = -2.0f * cosw0;
    *b2 = 1.0f - alpha * A;
    float a0 = 1.0f + alpha / A;
    *a1 = -2.0f * cosw0 / a0;
    *a2 = (1.0f - alpha / A) / a0;
    *b0 /= a0; *b1 /= a0; *b2 /= a0;
}

static const OrpheusParameter bank_params[] = {
    { .id = "fc0", .name = "Stage 0 Freq", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 1000.0f },
      .min_f32 = 20.0f, .max_f32 = 20000.0f, .unit = "Hz",
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .persistent = true },
    { .id = "q0", .name = "Stage 0 Q", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 1.0f },
      .min_f32 = 0.1f, .max_f32 = 10.0f,
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .persistent = true },
    { .id = "gain_db0", .name = "Stage 0 Gain", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.0f },
      .min_f32 = -24.0f, .max_f32 = 24.0f, .unit = "dB",
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .persistent = true },
    { .id = "fc1", .name = "Stage 1 Freq", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 3000.0f },
      .min_f32 = 20.0f, .max_f32 = 20000.0f, .unit = "Hz",
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .persistent = true },
    { .id = "q1", .name = "Stage 1 Q", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 1.0f },
      .min_f32 = 0.1f, .max_f32 = 10.0f,
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .persistent = true },
    { .id = "gain_db1", .name = "Stage 1 Gain", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.0f },
      .min_f32 = -24.0f, .max_f32 = 24.0f, .unit = "dB",
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .persistent = true },
    { .id = "channels", .name = "Channels", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32,
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true }
};

static const OrpheusPort bank_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor bank_descriptor = {
    .id = "orpheus.builtin.biquad_bank", .version = "1.0.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = bank_ports, .port_count = 2, .params = bank_params, .param_count = 7,
    .state_size = sizeof(BiquadBankState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = false
};

static const OrpheusComponentDescriptor* bank_get_descriptor(void) { return &bank_descriptor; }

static int bank_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(BiquadBankState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int bank_destroy(void* state) { (void)state; return ORPHEUS_OK; } /* v2：内存由 Runtime 统一管理 */

static int bank_prepare(void* state, const OrpheusConfig* config) {
    BiquadBankState* s = (BiquadBankState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    float fcs[BIQUAD_BANK_STAGES] = { 1000.0f, 3000.0f };
    float qs[BIQUAD_BANK_STAGES] = { 1.0f, 1.0f };
    float gs[BIQUAD_BANK_STAGES] = { 0.0f, 0.0f };
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (!config->param_ids[i]) continue;
        const char* id = config->param_ids[i];
        if (config->param_values[i].type != ORPHEUS_VALUE_FLOAT) continue;
        float v = config->param_values[i].value.f32;
        if (strcmp(id, "fc0") == 0) fcs[0] = v;
        else if (strcmp(id, "q0") == 0) qs[0] = v;
        else if (strcmp(id, "gain_db0") == 0) gs[0] = v;
        else if (strcmp(id, "fc1") == 0) fcs[1] = v;
        else if (strcmp(id, "q1") == 0) qs[1] = v;
        else if (strcmp(id, "gain_db1") == 0) gs[1] = v;
    }
    for (uint32_t i = 0; i < BIQUAD_BANK_STAGES; ++i) {
        BiquadState* bq = &s->bq[i];
        strncpy(bq->type, "peaking", sizeof(bq->type) - 1);
        bq->type[sizeof(bq->type) - 1] = '\0';
        bq->fc = fcs[i];
        bq->q = qs[i];
        bq->gain_db = gs[i];
        bank_peaking_coeffs(fcs[i], qs[i], gs[i], (float)config->sample_rate,
                            &bq->b0, &bq->b1, &bq->b2, &bq->a1, &bq->a2);
        for (uint32_t c = 0; c < MAX_CHANNELS; ++c) { bq->z1[c] = 0.0f; bq->z2[c] = 0.0f; }
    }
    return ORPHEUS_OK;
}
static int bank_reset(void* state) {
    BiquadBankState* s = (BiquadBankState*)state;
    for (uint32_t i = 0; i < BIQUAD_BANK_STAGES; ++i)
        for (uint32_t c = 0; c < MAX_CHANNELS; ++c) { s->bq[i].z1[c] = 0.0f; s->bq[i].z2[c] = 0.0f; }
    return ORPHEUS_OK;
}

static int bank_process(void* state, const OrpheusProcessContext* ctx) {
    BiquadBankState* s = (BiquadBankState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (in == NULL || out == NULL) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;
    /* 各级串联：x -> bq[0] -> bq[1] -> out */
    for (uint32_t c = 0; c < ch; ++c) {
        for (uint32_t n = 0; n < frames; ++n) {
            float x = in_data[n * ch + c];
            for (uint32_t i = 0; i < BIQUAD_BANK_STAGES; ++i) {
                BiquadState* bq = &s->bq[i];
                float y = bq->b0 * x + bq->b1 * bq->z1[c] + bq->b2 * bq->z2[c]
                          - bq->a1 * bq->z1[c] - bq->a2 * bq->z2[c];
                bq->z2[c] = bq->z1[c];
                bq->z1[c] = y;
                x = y;
            }
            out_data[n * ch + c] = x;
        }
    }
    out->frame_count = frames;
    return ORPHEUS_OK;
}

static int bank_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    (void)state; (void)param_id; (void)value;
    return ORPHEUS_ERR_UNSUPPORTED;  /* 全部 restart_required */
}
static int bank_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    BiquadBankState* s = (BiquadBankState*)state;
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)s->channels;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int bank_register_slots(void* state, const OrpheusRegistry* reg) {
    BiquadBankState* s = (BiquadBankState*)state;
    /* 父组件代理注册子块参数字段（层级键） */
    ORPHEUS_REG_SLOT(reg, &s->bq[0], fc, ORPHEUS_SLOT_SETTING, "fc0", "第 1 段频率",
                     ORPHEUS_VALUE_FLOAT, .min_f32=20.0f, .max_f32=20000.0f, .unit="Hz",
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT);
    ORPHEUS_REG_SLOT(reg, &s->bq[0], q, ORPHEUS_SLOT_SETTING, "q0", "第 1 段 Q",
                     ORPHEUS_VALUE_FLOAT, .min_f32=0.1f, .max_f32=10.0f,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT);
    ORPHEUS_REG_SLOT(reg, &s->bq[0], gain_db, ORPHEUS_SLOT_SETTING, "gain_db0", "第 1 段增益",
                     ORPHEUS_VALUE_FLOAT, .min_f32=-24.0f, .max_f32=24.0f, .unit="dB",
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT);
    ORPHEUS_REG_SLOT(reg, &s->bq[1], fc, ORPHEUS_SLOT_SETTING, "fc1", "第 2 段频率",
                     ORPHEUS_VALUE_FLOAT, .min_f32=20.0f, .max_f32=20000.0f, .unit="Hz",
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT);
    ORPHEUS_REG_SLOT(reg, &s->bq[1], q, ORPHEUS_SLOT_SETTING, "q1", "第 2 段 Q",
                     ORPHEUS_VALUE_FLOAT, .min_f32=0.1f, .max_f32=10.0f,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT);
    ORPHEUS_REG_SLOT(reg, &s->bq[1], gain_db, ORPHEUS_SLOT_SETTING, "gain_db1", "第 2 段增益",
                     ORPHEUS_VALUE_FLOAT, .min_f32=-24.0f, .max_f32=24.0f, .unit="dB",
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT);
    /* BULK 直写子块系数 buffer（b0..a2 连续 5 个 float），父代理注册 */
    OrpheusSlotInfo coef0 = {
        .kind = ORPHEUS_SLOT_BULK, .key = "bq0.coefs", .name = "第 1 段系数",
        .type = ORPHEUS_VALUE_FLOAT,
        .offset = (size_t)((char*)&s->bq[0].b0 - (char*)s),
        .size = sizeof(float), .count = 5,
        .flags = ORPHEUS_SLOT_DOUBLE_BUFFERED
    };
    reg->add(reg->ctx, &coef0);
    OrpheusSlotInfo coef1 = {
        .kind = ORPHEUS_SLOT_BULK, .key = "bq1.coefs", .name = "第 2 段系数",
        .type = ORPHEUS_VALUE_FLOAT,
        .offset = (size_t)((char*)&s->bq[1].b0 - (char*)s),
        .size = sizeof(float), .count = 5,
        .flags = ORPHEUS_SLOT_DOUBLE_BUFFERED
    };
    reg->add(reg->ctx, &coef1);
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface bank_interface = {
    .get_descriptor = bank_get_descriptor, .create = bank_create, .destroy = bank_destroy,
    .prepare = bank_prepare, .reset = bank_reset, .process = bank_process,
    .set_parameter = bank_set_parameter, .get_parameter = bank_get_parameter, .get_state_value = NULL,
    .register_slots = bank_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &bank_interface; }
