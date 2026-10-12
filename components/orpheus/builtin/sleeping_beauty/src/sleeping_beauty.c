#include "orpheus_sleeping_beauty.h"

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

static const char* read_string(const OrpheusConfig* config, const char* id, const char* fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_STRING) return config->param_values[i].value.str;
        }
    }
    return fallback;
}

static int32_t read_int(const OrpheusConfig* config, const char* id, int32_t fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_INT)   return config->param_values[i].value.i32;
            if (config->param_values[i].type == ORPHEUS_VALUE_FLOAT) return (int32_t)config->param_values[i].value.f32;
            if (config->param_values[i].type == ORPHEUS_VALUE_BOOL)  return config->param_values[i].value.b ? 1 : 0;
        }
    }
    return fallback;
}

/* 解析逗号分隔浮点表 */
static uint32_t parse_float_array(const char* str, float* out, uint32_t maxCount) {
    uint32_t count = 0;
    if (str == NULL) return 0;
    char buf[512];
    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char* tok = strtok(buf, ",");
    while (tok != NULL && count < maxCount) {
        out[count++] = (float)atof(tok);
        tok = strtok(NULL, ",");
    }
    return count;
}

/* 解析通道映射 */
static void parse_chan_map(const char* str, int32_t* chanMap, uint32_t channels) {
    for (uint32_t i = 0; i < SB_MAX_CHANNELS; ++i) chanMap[i] = 0;
    if (str == NULL) {
        for (uint32_t i = 0; i < channels; ++i) chanMap[i] = (int32_t)(i % SB_MAX_RAMPERS);
        return;
    }
    char buf[256];
    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char* tok = strtok(buf, ",");
    uint32_t idx = 0;
    while (tok != NULL && idx < channels) {
        int v = atoi(tok);
        if (v < 0 || v >= SB_MAX_RAMPERS) v = -1;
        chanMap[idx] = v;
        idx++;
        tok = strtok(NULL, ",");
    }
    while (idx < channels) { chanMap[idx] = -1; idx++; }
}

/* ---- 核心：calculate_SB_gains（源码 Model_1_1.c:13515-13670） ---- */

/* 锥度增益LUT查表 -> cut_linear；非对称L/R平衡锥度 -> targetGains[4] */
static void sb_calculate_gains(SleepingBeautyState* s, float targetGains[4]) {
    float gainIdx = s->gainIndex;
    float cut_linear = 0.0f;
    uint32_t j = 1;

    /* 1. TaperGainLUT 查表 */
    while (j <= s->tableSize) {
        float idxJ = s->tableIdx[j - 1];
        if (gainIdx <= idxJ) {
            if (j <= 1) {
                /* 首段：线性插值到零 */
                if (idxJ != 0.0f) {
                    cut_linear = (gainIdx / idxJ) * powf(10.0f, s->tableDb[0] / 20.0f);
                } else {
                    cut_linear = powf(10.0f, s->tableDb[0] / 20.0f);
                }
            } else {
                /* 其他段：dB 域线性插值 */
                float prevIdx = s->tableIdx[j - 2];
                float prevDb  = s->tableDb[j - 2];
                float currDb  = s->tableDb[j - 1];
                float denom = idxJ - prevIdx;
                float db;
                if (denom != 0.0f) {
                    float percent = (gainIdx - prevIdx) / denom;
                    db = percent * (currDb - prevDb) + prevDb;
                } else {
                    db = currDb;
                }
                cut_linear = powf(10.0f, db / 20.0f);
            }
            break;
        }
        j++;
    }

    /* 2. BalanceTaper：非对称 L/R 锥度 */
    float delta = gainIdx - s->offset;
    float left, right, center, mono;
    if (delta > 0.0f) {
        /* 左侧衰减 */
        left   = cut_linear;
        right  = 1.0f;
        center = cut_linear;
        mono   = cut_linear;
        if (fabsf(delta) >= s->offset - 1.0f) {
            left   = 0.0f;
            center = 0.0f;
        }
    } else {
        /* 右侧衰减 */
        right  = cut_linear;
        left   = 1.0f;
        center = cut_linear;
        mono   = cut_linear;
        if (fabsf(delta) >= s->offset - 1.0f) {
            right  = 0.0f;
            center = 0.0f;
        }
    }

    /* 3. SilentExtremeMutesBass */
    if (fabsf(delta) >= s->offset - 1.0f && s->mutesBass != 0.0f) {
        mono = 0.0f;
    }
    if (s->centerMonoMean && fabsf(delta) < s->offset - 1.0f) {
        center = (left + right) * 0.5f;
        mono = center;
    }

    targetGains[0] = left;
    targetGains[1] = right;
    targetGains[2] = center;
    targetGains[3] = mono;
}

/* ---- ramper 斜坡（与 gain_ramper 相同逻辑） ---- */

static void sb_ramper_set_target(SBRamper* r, float targetLinear,
                                  float rampMs, float rampDbPerSec,
                                  float sampleRate, uint32_t blockSize) {
    r->targetGain = fmaxf(targetLinear, SB_SILENT_GAIN);
    float curDb = 20.0f * log10f(fmaxf(r->currentGain, SB_SILENT_GAIN));
    float tgtDb = 20.0f * log10f(r->targetGain);
    float diff  = fabsf(tgtDb - curDb);
    if (diff < 0.01f || rampMs <= 0.0f || sampleRate <= 0.0f || blockSize == 0) {
        r->rampCoeff   = 1.0f;
        r->frameCount  = 0;
        r->currentGain = r->targetGain;
    } else {
        float effectiveRampMs = rampMs;
        if (rampDbPerSec > 0.0f) {
            float rate = fminf(50000.0f, fmaxf(5.0f, rampDbPerSec));
            effectiveRampMs = diff / rate * 1000.0f;
        }
        effectiveRampMs = fminf(3000.0f, fmaxf(1.0f, effectiveRampMs));
        float blockMs = 1000.0f * (float)blockSize / sampleRate;
        uint32_t numBlocks = (uint32_t)ceilf(effectiveRampMs / blockMs);
        if (numBlocks < 1) numBlocks = 1;
        r->frameCount = numBlocks;
        r->rampCoeff = expf(logf(r->targetGain / fmaxf(r->currentGain, SB_SILENT_GAIN)) /
                            ((float)blockSize * (float)numBlocks));
    }
}

/* 重新计算 4 路 target 并更新 ramper */
static void sb_recalc_targets(SleepingBeautyState* s) {
    float tg[4];
    sb_calculate_gains(s, tg);
    for (uint32_t i = 0; i < SB_MAX_RAMPERS; ++i) {
        sb_ramper_set_target(&s->rampers[i], tg[i], s->rampMs, s->rampDbPerSec,
                     s->sampleRate, s->blockSize);
    }
}

/* ---- 描述符 ---- */

static const OrpheusParameter sb_params[] = {
    { .id = "gain_index", .name = "Gain Index", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 128.0f },
      .min_f32 = 0.0f, .max_f32 = 255.0f,
      .update_policy = ORPHEUS_UPDATE_SMOOTHED,
      .readback = true, .persistent = true },
    { .id = "offset", .name = "Offset", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 128.0f },
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true },
    { .id = "mutes_bass", .name = "Mutes Bass", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.0f },
      .min_f32 = 0.0f, .max_f32 = 1.0f,
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true },
    { .id = "ramp_ms", .name = "Ramp Time", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 30.0f },
      .min_f32 = 0.0f, .max_f32 = 5000.0f, .unit = "ms",
    .update_policy = ORPHEUS_UPDATE_BLOCK_BOUNDARY,
      .readback = true, .persistent = true },
        { .id = "ramp_db_per_s", .name = "Ramp Rate", .type = ORPHEUS_VALUE_FLOAT,
            .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.0f },
            .min_f32 = 0.0f, .max_f32 = 50000.0f, .unit = "dB/s",
            .update_policy = ORPHEUS_UPDATE_BLOCK_BOUNDARY,
            .readback = true, .persistent = true },
        { .id = "freeze", .name = "Freeze", .type = ORPHEUS_VALUE_BOOL,
            .default_value = { .type = ORPHEUS_VALUE_BOOL, .value.b = false },
            .update_policy = ORPHEUS_UPDATE_BLOCK_BOUNDARY,
            .readback = true, .persistent = true },
        { .id = "center_mono_mode", .name = "Center/Mono Mode", .type = ORPHEUS_VALUE_STRING,
            .default_value = { .type = ORPHEUS_VALUE_STRING, .value.str = "attenuated" },
            .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
            .readback = true, .persistent = true },
        { .id = "apply_index_on_prepare", .name = "Apply Index On Prepare", .type = ORPHEUS_VALUE_BOOL,
            .default_value = { .type = ORPHEUS_VALUE_BOOL, .value.b = true },
            .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
            .readback = true, .persistent = true },
        { .id = "preserve_state_on_reset", .name = "Preserve State On Reset", .type = ORPHEUS_VALUE_BOOL,
            .default_value = { .type = ORPHEUS_VALUE_BOOL, .value.b = false },
            .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
            .readback = true, .persistent = true },
    { .id = "channels", .name = "Channels", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 4 },
    .min_i32 = 1, .max_i32 = 64,
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "chan_map", .name = "Channel Map", .type = ORPHEUS_VALUE_STRING,
      .default_value = { .type = ORPHEUS_VALUE_STRING, .value.str = "0,1,2,3" },
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true },
    { .id = "table_idx", .name = "Table Index", .type = ORPHEUS_VALUE_STRING,
      .default_value = { .type = ORPHEUS_VALUE_STRING, .value.str = "0,10,31,52,74,95,116,128,138,159,180,202,223,244,255" },
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true },
    { .id = "table_db", .name = "Table dB", .type = ORPHEUS_VALUE_STRING,
      .default_value = { .type = ORPHEUS_VALUE_STRING, .value.str = "-40,-30,-20,-10,0,0,0,0,0,0,0,-10,-20,-30,-40" },
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true }
};

static const OrpheusPort sb_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor sb_descriptor = {
    .id = "orpheus.builtin.sleeping_beauty", .version = "1.2.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = sb_ports, .port_count = 2, .params = sb_params, .param_count = 13,
    .state_size = sizeof(SleepingBeautyState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = true
};

static const OrpheusComponentDescriptor* sb_get_descriptor(void) { return &sb_descriptor; }

/* ---- 生命周期 ---- */

static int sb_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(SleepingBeautyState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}

static int sb_destroy(void* state) { (void)state; return ORPHEUS_OK; }

static int sb_prepare(void* state, const OrpheusConfig* config) {
    SleepingBeautyState* s = (SleepingBeautyState*)state;
    s->channels   = config->channels > 0 ? config->channels : 4;
    s->gainIndex  = read_float(config, "gain_index", 128.0f);
    s->offset     = read_float(config, "offset", 128.0f);
    s->mutesBass  = read_float(config, "mutes_bass", 0.0f);
    s->rampMs     = read_float(config, "ramp_ms", 30.0f);
    s->rampDbPerSec = read_float(config, "ramp_db_per_s", 0.0f);
    s->freeze = read_int(config, "freeze", 0) != 0;
    s->centerMonoMean = strcmp(read_string(config, "center_mono_mode", "attenuated"), "mean_lr") == 0;
    s->applyIndexOnPrepare = read_int(config, "apply_index_on_prepare", 1) != 0;
    s->preserveStateOnReset = read_int(config, "preserve_state_on_reset", 0) != 0;
    s->controlsDirty = 0;
    s->sampleRate = config->sample_rate > 0 ? (float)config->sample_rate : 48000.0f;
    s->blockSize  = config->block_size > 0 ? config->block_size : 32;

    /* 解析查表 */
    const char* idxStr = read_string(config, "table_idx",
        "0,10,31,52,74,95,116,128,138,159,180,202,223,244,255");
    const char* dbStr  = read_string(config, "table_db",
        "-40,-30,-20,-10,0,0,0,0,0,0,0,-10,-20,-30,-40");
    uint32_t nIdx = parse_float_array(idxStr, s->tableIdx, SB_MAX_TABLE);
    uint32_t nDb  = parse_float_array(dbStr, s->tableDb, SB_MAX_TABLE);
    s->tableSize = nIdx < nDb ? nIdx : nDb;
    if (s->tableSize == 0) s->tableSize = 1;

    /* 解析通道映射 */
    const char* mapStr = read_string(config, "chan_map", "0,1,2,3");
    parse_chan_map(mapStr, s->chanMap, s->channels);

    /* 初始化 ramper 并计算目标增益 */
    for (uint32_t i = 0; i < SB_MAX_RAMPERS; ++i) {
        s->rampers[i].currentGain = 1.0f;
        s->rampers[i].targetGain  = 1.0f;
        s->rampers[i].rampCoeff   = 1.0f;
        s->rampers[i].frameCount  = 0;
    }
    if (s->applyIndexOnPrepare) {
        sb_recalc_targets(s);
        for (uint32_t i = 0; i < SB_MAX_RAMPERS; ++i) {
            s->rampers[i].currentGain = s->rampers[i].targetGain;
            s->rampers[i].rampCoeff   = 1.0f;
            s->rampers[i].frameCount  = 0;
        }
    }
    return ORPHEUS_OK;
}

static int sb_reset(void* state) {
    SleepingBeautyState* s = (SleepingBeautyState*)state;
    if (s->preserveStateOnReset) return ORPHEUS_OK;
    for (uint32_t i = 0; i < SB_MAX_RAMPERS; ++i) {
        s->rampers[i].currentGain = 1.0f;
        s->rampers[i].targetGain = 1.0f;
        s->rampers[i].rampCoeff   = 1.0f;
        s->rampers[i].frameCount  = 0;
    }
    if (s->applyIndexOnPrepare) {
        s->gainIndex = s->offset;
        sb_recalc_targets(s);
        for (uint32_t i = 0; i < SB_MAX_RAMPERS; ++i) {
            s->rampers[i].currentGain = s->rampers[i].targetGain;
            s->rampers[i].rampCoeff = 1.0f;
            s->rampers[i].frameCount = 0;
        }
    }
    s->controlsDirty = 0;
    return ORPHEUS_OK;
}

/* ---- 实时处理 ---- */

static int sb_process(void* state, const OrpheusProcessContext* ctx) {
    SleepingBeautyState* s = (SleepingBeautyState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (in == NULL || out == NULL) return ORPHEUS_ERR_INVALID_ARG;

    if (s->controlsDirty) {
        sb_recalc_targets(s);
        s->controlsDirty = 0;
    }

    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;

    /* 每个样本推进四路共享 ramper，再应用到全部映射通道。 */
    for (uint32_t n = 0; n < frames; ++n) {
        if (!s->freeze) {
            for (uint32_t r = 0; r < SB_MAX_RAMPERS; ++r) {
                SBRamper* rp = &s->rampers[r];
                if (rp->frameCount > 0) rp->currentGain *= rp->rampCoeff;
                if (rp->currentGain < SB_SILENT_GAIN) rp->currentGain = SB_SILENT_GAIN;
            }
        }
        for (uint32_t c = 0; c < ch; ++c) {
            int32_t ri = s->chanMap[c];
            float g = (ri >= 0 && ri < SB_MAX_RAMPERS) ? s->rampers[ri].currentGain : 0.0f;
            out_data[n * ch + c] = in_data[n * ch + c] * g;
        }
    }
    if (!s->freeze) {
        for (uint32_t r = 0; r < SB_MAX_RAMPERS; ++r) {
            SBRamper* rp = &s->rampers[r];
            if (rp->frameCount > 0 && --rp->frameCount == 0) {
                rp->currentGain = rp->targetGain;
                rp->rampCoeff = 1.0f;
            }
        }
    }
    out->frame_count = frames;
    return ORPHEUS_OK;
}

/* ---- 参数 ---- */

static int sb_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    SleepingBeautyState* s = (SleepingBeautyState*)state;
    if (strcmp(param_id, "gain_index") == 0) {
        if (value->type != ORPHEUS_VALUE_FLOAT && value->type != ORPHEUS_VALUE_INT)
            return ORPHEUS_ERR_INVALID_ARG;
        s->gainIndex = (value->type == ORPHEUS_VALUE_FLOAT) ? value->value.f32 : (float)value->value.i32;
        s->controlsDirty = 1;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "ramp_db_per_s") == 0) {
        if (value->type != ORPHEUS_VALUE_FLOAT && value->type != ORPHEUS_VALUE_INT)
            return ORPHEUS_ERR_INVALID_ARG;
        s->rampDbPerSec = (value->type == ORPHEUS_VALUE_FLOAT) ? value->value.f32 : (float)value->value.i32;
        s->controlsDirty = 1;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "ramp_ms") == 0) {
        if (value->type != ORPHEUS_VALUE_FLOAT && value->type != ORPHEUS_VALUE_INT)
            return ORPHEUS_ERR_INVALID_ARG;
        s->rampMs = (value->type == ORPHEUS_VALUE_FLOAT) ? value->value.f32 : (float)value->value.i32;
        s->controlsDirty = 1;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "freeze") == 0) {
        if (value->type != ORPHEUS_VALUE_BOOL && value->type != ORPHEUS_VALUE_INT)
            return ORPHEUS_ERR_INVALID_ARG;
        s->freeze = (value->type == ORPHEUS_VALUE_BOOL) ? value->value.b : value->value.i32 != 0;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int sb_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    SleepingBeautyState* s = (SleepingBeautyState*)state;
    if (strcmp(param_id, "gain_index") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT; value->value.f32 = s->gainIndex; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "ramp_db_per_s") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT; value->value.f32 = s->rampDbPerSec; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "ramp_ms") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT; value->value.f32 = s->rampMs; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "freeze") == 0) {
        value->type = ORPHEUS_VALUE_BOOL; value->value.b = s->freeze != 0; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "center_mono_mode") == 0) {
        value->type = ORPHEUS_VALUE_STRING;
        value->value.str = s->centerMonoMean ? "mean_lr" : "attenuated";
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "apply_index_on_prepare") == 0) {
        value->type = ORPHEUS_VALUE_BOOL; value->value.b = s->applyIndexOnPrepare != 0; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "preserve_state_on_reset") == 0) {
        value->type = ORPHEUS_VALUE_BOOL; value->value.b = s->preserveStateOnReset != 0; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->channels; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

/* ---- 槽注册 ---- */

static int sb_register_slots(void* state, const OrpheusRegistry* reg) {
    SleepingBeautyState* s = (SleepingBeautyState*)state;
    ORPHEUS_REG_SLOT(reg, s, gainIndex, ORPHEUS_SLOT_SETTING, "gain_index", "增益位置",
                     ORPHEUS_VALUE_FLOAT, .min_f32=0.0f, .max_f32=255.0f,
                     .update_policy=ORPHEUS_UPDATE_SMOOTHED,
                    .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, offset, ORPHEUS_SLOT_SETTING, "offset", "中心位置",
                     ORPHEUS_VALUE_FLOAT, .min_f32=0.0f, .max_f32=255.0f,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, mutesBass, ORPHEUS_SLOT_SETTING, "mutes_bass", "极端静音低音",
                     ORPHEUS_VALUE_FLOAT, .min_f32=0.0f, .max_f32=1.0f,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, rampMs, ORPHEUS_SLOT_SETTING, "ramp_ms", "斜坡时间",
                     ORPHEUS_VALUE_FLOAT, .min_f32=0.0f, .max_f32=5000.0f, .unit="ms",
                     .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=64,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, rampDbPerSec, ORPHEUS_SLOT_SETTING, "ramp_db_per_s", "斜坡速率",
                     ORPHEUS_VALUE_FLOAT, .min_f32=0.0f, .max_f32=50000.0f, .unit="dB/s",
                     .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, freeze, ORPHEUS_SLOT_SETTING, "freeze", "冻结斜坡",
                     ORPHEUS_VALUE_BOOL, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    return ORPHEUS_OK;
}

/* ---- 接口 ---- */

static const OrpheusComponentInterface sb_interface = {
    .get_descriptor = sb_get_descriptor, .create = sb_create, .destroy = sb_destroy,
    .prepare = sb_prepare, .reset = sb_reset, .process = sb_process,
    .set_parameter = sb_set_parameter, .get_parameter = sb_get_parameter,
    .get_state_value = NULL, .register_slots = sb_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &sb_interface; }
