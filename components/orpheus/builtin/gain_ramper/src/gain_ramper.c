#include "orpheus_gain_ramper.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- 辅助函数 ---- */

static float read_float(const OrpheusConfig* config, const char* id, float fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_FLOAT) return config->param_values[i].value.f32;
            if (config->param_values[i].type == ORPHEUS_VALUE_INT)   return (float)config->param_values[i].value.i32;
        }
    }
    return fallback;
}

static int32_t read_int(const OrpheusConfig* config, const char* id, int32_t fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_INT)   return config->param_values[i].value.i32;
            if (config->param_values[i].type == ORPHEUS_VALUE_FLOAT) return (int32_t)config->param_values[i].value.f32;
        }
    }
    return fallback;
}

static const char* read_string(const OrpheusConfig* config, const char* id, const char* fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_STRING) return config->param_values[i].value.str;
        }
    }
    return fallback;
}

static float db_to_linear(float db) { return powf(10.0f, db / 20.0f); }

/* 解析通道映射字符串 "0,1,0,1,2,3,-1" -> chanMap[] */
static void parse_chan_map(const char* str, int32_t* chanMap, uint32_t channels, uint32_t numRampers) {
    for (uint32_t i = 0; i < GR_MAX_CHANNELS; ++i) chanMap[i] = 0;
    if (str == NULL) {
        for (uint32_t i = 0; i < channels; ++i) chanMap[i] = (int32_t)(i % numRampers);
        return;
    }
    char buf[256];
    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char* tok = strtok(buf, ",");
    uint32_t idx = 0;
    while (tok != NULL && idx < channels) {
        int v = atoi(tok);
        if (v < 0) v = -1;               /* -1 = bypass（unity gain） */
        if (v >= (int)numRampers) v = 0;  /* 越界回退到 ramper 0 */
        chanMap[idx] = v;
        idx++;
        tok = strtok(NULL, ",");
    }
    /* 未指定的通道默认映射到 ramper 0 */
    while (idx < channels) { chanMap[idx] = 0; idx++; }
}

/* 更新 ramper 目标并计算斜坡系数（dB 域恒定速率） */
static void ramper_set_target(RamperSlot* r, float targetLinear,
                               float rampMs, float sampleRate, uint32_t blockSize) {
    r->targetGain = fmaxf(targetLinear, GR_SILENT_GAIN);
    float curDb  = 20.0f * log10f(fmaxf(r->currentGain, GR_SILENT_GAIN));
    float tgtDb  = 20.0f * log10f(r->targetGain);
    float diff   = fabsf(tgtDb - curDb);
    if (diff < 0.01f || rampMs <= 0.0f || sampleRate <= 0.0f || blockSize == 0) {
        r->rampCoeff  = 0.0f;
        r->currentGain = r->targetGain;
    } else {
        float numBlocks = rampMs / 1000.0f * sampleRate / (float)blockSize;
        if (numBlocks < 1.0f) numBlocks = 1.0f;
        r->rampCoeff = logf(r->targetGain / fmaxf(r->currentGain, GR_SILENT_GAIN)) / numBlocks;
    }
}

/* ---- 描述符 ---- */

static const OrpheusParameter gr_params[] = {
    { .id = "gain_db", .name = "Target Gain", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.0f },
      .min_f32 = -96.0f, .max_f32 = 24.0f, .unit = "dB",
      .update_policy = ORPHEUS_UPDATE_SMOOTHED,
      .readback = true, .persistent = true },
    { .id = "ramp_ms", .name = "Ramp Time", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 30.0f },
      .min_f32 = 0.0f, .max_f32 = 5000.0f, .unit = "ms",
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true },
    { .id = "channels", .name = "Channels", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32,
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "num_rampers", .name = "Num Rampers", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 1 },
      .min_i32 = 1, .max_i32 = 8,
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true },
    { .id = "chan_map", .name = "Channel Map", .type = ORPHEUS_VALUE_STRING,
      .default_value = { .type = ORPHEUS_VALUE_STRING, .value.str = "0,0" },
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true }
};

static const OrpheusPort gr_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor gr_descriptor = {
    .id = "orpheus.builtin.gain_ramper", .version = "1.0.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = gr_ports, .port_count = 2, .params = gr_params, .param_count = 5,
    .state_size = sizeof(GainRamperState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = true
};

static const OrpheusComponentDescriptor* gr_get_descriptor(void) { return &gr_descriptor; }

/* ---- 生命周期 ---- */

static int gr_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(GainRamperState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}

static int gr_destroy(void* state) { (void)state; return ORPHEUS_OK; }

static int gr_prepare(void* state, const OrpheusConfig* config) {
    GainRamperState* s = (GainRamperState*)state;
    s->channels   = config->channels > 0 ? config->channels : 2;
    s->numRampers = (uint32_t)read_int(config, "num_rampers", 1);
    if (s->numRampers < 1) s->numRampers = 1;
    if (s->numRampers > GR_MAX_RAMPERS) s->numRampers = GR_MAX_RAMPERS;
    s->ramp_ms      = read_float(config, "ramp_ms", 30.0f);
    s->gain_db      = read_float(config, "gain_db", 0.0f);
    s->sampleRate   = config->sample_rate > 0 ? (float)config->sample_rate : 48000.0f;
    s->blockSize    = config->block_size > 0 ? config->block_size : 32;

    /* 解析通道映射 */
    const char* mapStr = read_string(config, "chan_map", "0,0");
    parse_chan_map(mapStr, s->chanMap, s->channels, s->numRampers);

    /* 初始化所有 ramper 到目标增益 */
    float targetLin = db_to_linear(s->gain_db);
    for (uint32_t i = 0; i < s->numRampers; ++i) {
        s->rampers[i].currentGain = targetLin;
        s->rampers[i].targetGain  = targetLin;
        s->rampers[i].rampCoeff   = 0.0f;
    }
    return ORPHEUS_OK;
}

static int gr_reset(void* state) {
    GainRamperState* s = (GainRamperState*)state;
    float targetLin = db_to_linear(s->gain_db);
    for (uint32_t i = 0; i < s->numRampers; ++i) {
        s->rampers[i].currentGain = targetLin;
        s->rampers[i].targetGain  = targetLin;
        s->rampers[i].rampCoeff   = 0.0f;
    }
    return ORPHEUS_OK;
}

/* ---- 实时处理 ---- */

static int gr_process(void* state, const OrpheusProcessContext* ctx) {
    GainRamperState* s = (GainRamperState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (in == NULL || out == NULL) return ORPHEUS_ERR_INVALID_ARG;

    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;

    /* 每块更新一次 ramper（与 Rgainx 一致：per-frame 而非 per-sample） */
    for (uint32_t r = 0; r < s->numRampers; ++r) {
        RamperSlot* rp = &s->rampers[r];
        if (rp->rampCoeff != 0.0f) {
            rp->currentGain *= expf(rp->rampCoeff);
            /* 到达或越过目标则吸附 */
            if (rp->rampCoeff > 0.0f && rp->currentGain >= rp->targetGain) {
                rp->currentGain = rp->targetGain;
                rp->rampCoeff = 0.0f;
            } else if (rp->rampCoeff < 0.0f && rp->currentGain <= rp->targetGain) {
                rp->currentGain = rp->targetGain;
                rp->rampCoeff = 0.0f;
            }
        }
        if (rp->currentGain < GR_SILENT_GAIN) rp->currentGain = GR_SILENT_GAIN;
    }

    /* 应用增益：每通道按映射的 ramper currentGain 相乘 */
    for (uint32_t n = 0; n < frames; ++n) {
        for (uint32_t c = 0; c < ch; ++c) {
            int32_t ri = s->chanMap[c];
            float g = (ri >= 0 && ri < (int32_t)s->numRampers) ? s->rampers[ri].currentGain : 1.0f;
            out_data[n * ch + c] = in_data[n * ch + c] * g;
        }
    }
    out->frame_count = frames;
    return ORPHEUS_OK;
}

/* ---- 参数 ---- */

static int gr_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    GainRamperState* s = (GainRamperState*)state;
    if (strcmp(param_id, "gain_db") == 0) {
        if (value->type != ORPHEUS_VALUE_FLOAT && value->type != ORPHEUS_VALUE_INT)
            return ORPHEUS_ERR_INVALID_ARG;
        s->gain_db = (value->type == ORPHEUS_VALUE_FLOAT) ? value->value.f32 : (float)value->value.i32;
        float targetLin = db_to_linear(s->gain_db);
        for (uint32_t i = 0; i < s->numRampers; ++i) {
            ramper_set_target(&s->rampers[i], targetLin, s->ramp_ms, s->sampleRate, s->blockSize);
        }
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int gr_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    GainRamperState* s = (GainRamperState*)state;
    if (strcmp(param_id, "gain_db") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT; value->value.f32 = s->gain_db; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->channels; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "num_rampers") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->numRampers; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

/* ---- 槽注册 ---- */

static int gr_register_slots(void* state, const OrpheusRegistry* reg) {
    GainRamperState* s = (GainRamperState*)state;
    ORPHEUS_REG_SLOT(reg, s, gain_db, ORPHEUS_SLOT_SETTING, "gain_db", "目标增益",
                     ORPHEUS_VALUE_FLOAT, .min_f32=-96.0f, .max_f32=24.0f, .unit="dB",
                     .update_policy=ORPHEUS_UPDATE_SMOOTHED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_DIRECT_WRITE);
    ORPHEUS_REG_SLOT(reg, s, ramp_ms, ORPHEUS_SLOT_SETTING, "ramp_ms", "斜坡时间",
                     ORPHEUS_VALUE_FLOAT, .min_f32=0.0f, .max_f32=5000.0f, .unit="ms",
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, numRampers, ORPHEUS_SLOT_SETTING, "num_rampers", "ramper 数量",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=8,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    return ORPHEUS_OK;
}

/* ---- 接口 ---- */

static const OrpheusComponentInterface gr_interface = {
    .get_descriptor = gr_get_descriptor, .create = gr_create, .destroy = gr_destroy,
    .prepare = gr_prepare, .reset = gr_reset, .process = gr_process,
    .set_parameter = gr_set_parameter, .get_parameter = gr_get_parameter,
    .get_state_value = NULL, .register_slots = gr_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &gr_interface; }
