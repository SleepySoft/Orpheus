#include "orpheus_gain.h"

#include <math.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static float db_to_linear(float db) {
    return powf(10.0f, db / 20.0f);
}

static const OrpheusParameter gain_params[] = {
    {
        .id = "gain_db",
        .name = "Gain",
        .type = ORPHEUS_VALUE_FLOAT,
        .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.0f },
        .min_f32 = -96.0f,
        .max_f32 = 24.0f,
        .unit = "dB",
        .update_policy = ORPHEUS_UPDATE_SMOOTHED,
        .readback = true,
        .persistent = true,
        .affects_signature = false
    },
    {
        .id = "channels",
        .name = "Channels",
        .type = ORPHEUS_VALUE_INT,
        .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
        .min_i32 = 1,
        .max_i32 = 32,
        .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
        .readback = true,
        .persistent = true,
        .affects_signature = true
    },
    {
        .id = "smoothing_ms",
        .name = "Smoothing Time",
        .type = ORPHEUS_VALUE_FLOAT,
        .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 5.0f },
        .min_f32 = 0.0f,
        .max_f32 = 1000.0f,
        .unit = "ms",
        .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
        .readback = true,
        .persistent = true,
        .affects_signature = false
    },
    { .id = "gain_mode", .name = "增益模式（0=dB，1=共享线性，2=逐通道线性）", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 0 },
      .min_i32 = 0, .max_i32 = 2, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true },
    { .id = "gain_linear", .name = "线性目标增益", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 1.0f },
      .update_policy = ORPHEUS_UPDATE_SMOOTHED, .readback = true, .persistent = true },
    { .id = "linear_b0", .name = "线性控制平滑分子", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 1.0f },
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED, .readback = true, .persistent = true },
    { .id = "linear_a1", .name = "线性控制平滑反馈系数", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.0f },
      .min_f32 = -1.0f, .max_f32 = 1.0f,
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED, .readback = true, .persistent = true },
    { .id = "gains", .name = "逐通道线性增益", .type = ORPHEUS_VALUE_STRING,
      .default_value = { .type = ORPHEUS_VALUE_STRING, .value.str = "" },
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED, .readback = true, .persistent = true }
};

static const OrpheusPort gain_ports[] = {
    {
        .id = "in",
        .direction = ORPHEUS_PORT_INPUT,
        .type = ORPHEUS_PORT_AUDIO,
        .sample_format = ORPHEUS_FORMAT_F32,
        .channels = 0,
        .sample_rate = 0,
        .block_size = 0,
        .is_variable = true,
        .channels_param = "channels"
    },
    {
        .id = "out",
        .direction = ORPHEUS_PORT_OUTPUT,
        .type = ORPHEUS_PORT_AUDIO,
        .sample_format = ORPHEUS_FORMAT_F32,
        .channels = 0,
        .sample_rate = 0,
        .block_size = 0,
        .is_variable = true,
        .channels_param = "channels"
    }
};

static const OrpheusComponentDescriptor gain_descriptor = {
    .id = "orpheus.builtin.gain",
    .version = "1.2.0",
    .abi_version = ORPHEUS_ABI_VERSION,
    .ports = gain_ports,
    .port_count = 2,
    .params = gain_params,
    .param_count = 8,
    .state_size = sizeof(GainState),
    .scratch_size = 0,
    .alignment = 8,
    .latency_samples = 0,
    .realtime_safe = true,
    .supports_inplace = true
};

static const OrpheusComponentDescriptor* gain_get_descriptor_impl(void) {
    return &gain_descriptor;
}

static int gain_create(void** state, const OrpheusConfig* config) {
    /* v2：统一内存拼接下发的实例块；未提供时回退 calloc（旧宿主兼容） */
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(GainState));
    if (*state == NULL) {
        return ORPHEUS_ERR_OUT_OF_MEMORY;
    }
    return ORPHEUS_OK;
}

static int gain_destroy(void* state) {
    (void)state; /* v2：状态内存由 Runtime 统一管理 */
    return ORPHEUS_OK;
}

static int gain_prepare(void* state, const OrpheusConfig* config) {
    if (!state || !config) return ORPHEUS_ERR_INVALID_ARG;
    GainState* s = (GainState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    if (s->channels > 32) return ORPHEUS_ERR_INVALID_ARG;
    const char* channel_gains = "";

    /* 初始参数：gain_db 与 smoothing_ms 都从 prepare 配置读取 */
    float gain_db = 0.0f;
    float smoothing_ms = 5.0f;
    s->gain_mode = 0;
    s->linear_value = 1.0f;
    s->linear_b0 = 1.0f;
    s->linear_a1 = 0.0f;
    s->linear_state = 0.0f;
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] == NULL) continue;
        const char* id = config->param_ids[i];
        const OrpheusValue* value = &config->param_values[i];
        if (strcmp(id, "gain_mode") == 0) {
            if (value->type != ORPHEUS_VALUE_INT || value->value.i32 < 0 ||
                value->value.i32 > 2) return ORPHEUS_ERR_INVALID_ARG;
            s->gain_mode = value->value.i32;
        } else if (strcmp(id, "gains") == 0) {
            if (value->type != ORPHEUS_VALUE_STRING || !value->value.str)
                return ORPHEUS_ERR_INVALID_ARG;
            channel_gains = value->value.str;
        } else if (strcmp(id, "gain_linear") == 0 || strcmp(id, "linear_b0") == 0 ||
                   strcmp(id, "linear_a1") == 0) {
            if (value->type != ORPHEUS_VALUE_FLOAT || !isfinite(value->value.f32))
                return ORPHEUS_ERR_INVALID_ARG;
            if (strcmp(id, "gain_linear") == 0) s->linear_value = value->value.f32;
            else if (strcmp(id, "linear_b0") == 0) s->linear_b0 = value->value.f32;
            else s->linear_a1 = value->value.f32;
        }
        if (*channel_gains && s->gain_mode != 2) return ORPHEUS_ERR_INVALID_ARG;
        if (*channel_gains) {
            const char* cursor = channel_gains;
            for (uint32_t c = 0; c < s->channels; ++c) {
                char* end;
                float gain = strtof(cursor, &end);
                if (end == cursor || !isfinite(gain)) return ORPHEUS_ERR_INVALID_ARG;
                while (isspace((unsigned char)*end)) ++end;
                if (c+1 < s->channels ? *end != ',' : *end != '\0') return ORPHEUS_ERR_INVALID_ARG;
                s->per_channel_gains[c] = gain;
                cursor = c+1 < s->channels ? end+1 : end;
            }
        } else {
            for (uint32_t c = 0; c < s->channels; ++c) s->per_channel_gains[c] = 1.0f;
        }
        if (strcmp(config->param_ids[i], "gain_db") == 0 &&
            config->param_values[i].type == ORPHEUS_VALUE_FLOAT) {
            gain_db = config->param_values[i].value.f32;
        } else if (strcmp(config->param_ids[i], "smoothing_ms") == 0 &&
                   config->param_values[i].type == ORPHEUS_VALUE_FLOAT) {
            smoothing_ms = config->param_values[i].value.f32;
        }
        if (s->linear_a1 <= -1.0f || s->linear_a1 >= 1.0f)
            return ORPHEUS_ERR_INVALID_ARG;
    }
    s->gain_db = gain_db;
    s->smoothing_ms = smoothing_ms;
    s->gain_linear = s->gain_mode == 1 ? 0.0f : db_to_linear(gain_db);
    s->target_linear = s->gain_mode == 1 ? s->linear_value : s->gain_linear;

    if (smoothing_ms <= 0.0f || config->sample_rate == 0) {
        s->smoothing_coeff = 1.0f;
    } else {
        float tau = smoothing_ms / 1000.0f;
        s->smoothing_coeff = 1.0f - expf(-1.0f / (tau * (float)config->sample_rate));
        if (s->smoothing_coeff > 1.0f) s->smoothing_coeff = 1.0f;
    }

    return ORPHEUS_OK;
}

static int gain_reset(void* state) {
    GainState* s = (GainState*)state;
    if (s->gain_mode == 2) return ORPHEUS_OK;
    if (s->gain_mode == 1) {
        s->linear_state = 0.0f;
        s->gain_linear = 0.0f;
        return ORPHEUS_OK;
    }
    s->gain_db = 0.0f;
    s->gain_linear = db_to_linear(0.0f);
    s->target_linear = s->gain_linear;
    return ORPHEUS_OK;
}

static int gain_process(void* state, const OrpheusProcessContext* ctx) {
    GainState* s = (GainState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];

    if (in == NULL || out == NULL) {
        return ORPHEUS_ERR_INVALID_ARG;
    }

    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    float* in_data = (float*)in->data;
    float* out_data = (float*)out->data;

    if (s->gain_mode == 2) {
        for (uint32_t n = 0; n < frames; ++n)
            for (uint32_t c = 0; c < ch; ++c)
                out_data[n*ch+c] = in_data[n*ch+c]*s->per_channel_gains[c];
        out->frame_count = frames;
        return orpheus_process_next(ctx);
    }
    if (s->gain_mode == 1) {
        for (uint32_t n = 0; n < frames; ++n) {
            float gain = s->linear_value;
            if (s->linear_b0 != 1.0f || s->linear_a1 != 0.0f) {
                gain = s->linear_b0 * s->linear_value + s->linear_state;
                s->linear_state = 0.0f - s->linear_a1 * gain;
            }
            s->gain_linear = gain;
            for (uint32_t c = 0; c < ch; ++c)
                out_data[n * ch + c] = in_data[n * ch + c] * gain;
        }
        out->frame_count = frames;
        return orpheus_process_next(ctx);
    }

    /* 平滑过渡增益 */
    float target = db_to_linear(s->gain_db);
    s->target_linear = target;
    for (uint32_t i = 0; i < frames * ch; ++i) {
        s->gain_linear += s->smoothing_coeff * (target - s->gain_linear);
        out_data[i] = in_data[i] * s->gain_linear;
    }

    out->frame_count = frames;
    return orpheus_process_next(ctx);
}

static int gain_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    GainState* s = (GainState*)state;
    if (!s || !param_id || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(param_id, "gain_linear") == 0) {
        if (s->gain_mode != 1) return ORPHEUS_ERR_UNSUPPORTED;
        if (value->type != ORPHEUS_VALUE_FLOAT || !isfinite(value->value.f32))
            return ORPHEUS_ERR_INVALID_ARG;
        s->linear_value = value->value.f32;
        s->target_linear = value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "gain_db") == 0) {
        if (s->gain_mode != 0) return ORPHEUS_ERR_UNSUPPORTED;
        if (value->type != ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        s->gain_db = value->value.f32;
        s->target_linear = db_to_linear(value->value.f32);
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int gain_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    GainState* s = (GainState*)state;
    if (!s || !param_id || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(param_id, "gain_mode") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = s->gain_mode;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "gain_linear") == 0 || strcmp(param_id, "linear_b0") == 0 ||
        strcmp(param_id, "linear_a1") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT;
        value->value.f32 = strcmp(param_id, "gain_linear") == 0 ? s->linear_value
            : strcmp(param_id, "linear_b0") == 0 ? s->linear_b0 : s->linear_a1;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "gain_db") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT;
        value->value.f32 = s->gain_db;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)s->channels;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int gain_register_slots(void* state, const OrpheusRegistry* reg) {
    GainState* s = (GainState*)state;
    ORPHEUS_REG_SLOT(reg, s, gain_db, ORPHEUS_SLOT_SETTING, "gain_db", "增益",
                     ORPHEUS_VALUE_FLOAT, .min_f32=-96.0f, .max_f32=24.0f, .unit="dB",
                     .update_policy=ORPHEUS_UPDATE_SMOOTHED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            (s->gain_mode == 0 ? ORPHEUS_SLOT_DIRECT_WRITE : 0));
    ORPHEUS_REG_SLOT(reg, s, smoothing_ms, ORPHEUS_SLOT_SETTING, "smoothing_ms", "平滑时间",
                     ORPHEUS_VALUE_FLOAT, .min_f32=0.0f, .max_f32=1000.0f, .unit="ms",
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, gain_mode, ORPHEUS_SLOT_SETTING, "gain_mode", "增益模式",
                     ORPHEUS_VALUE_INT, .min_i32=0, .max_i32=2,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, linear_value, ORPHEUS_SLOT_SETTING, "gain_linear", "线性目标增益",
                     ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_SMOOTHED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, linear_b0, ORPHEUS_SLOT_SETTING, "linear_b0", "线性控制平滑分子",
                     ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, linear_a1, ORPHEUS_SLOT_SETTING, "linear_a1", "线性控制平滑反馈系数",
                     ORPHEUS_VALUE_FLOAT, .min_f32=-1.0f, .max_f32=1.0f,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    if (s->gain_mode == 2) {
        OrpheusSlotInfo slot = {
            .kind=ORPHEUS_SLOT_BULK, .key="gains", .name="逐通道线性增益",
            .type=ORPHEUS_VALUE_FLOAT, .offset=offsetof(GainState, per_channel_gains),
            .size=sizeof(float), .count=s->channels, .flags=ORPHEUS_SLOT_DOUBLE_BUFFERED
        };
        reg->add(reg->ctx, &slot);
    }
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface gain_interface = {
    .get_descriptor = gain_get_descriptor_impl,
    .create = gain_create,
    .destroy = gain_destroy,
    .prepare = gain_prepare,
    .reset = gain_reset,
    .process = gain_process,
    .set_parameter = gain_set_parameter,
    .get_parameter = gain_get_parameter,
    .get_state_value = NULL,
    .register_slots = gain_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {
    return &gain_interface;
}
