#include "orpheus_input_mixer_3d.h"

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

/* ---- 描述符 ---- */

static const OrpheusParameter im3d_params[] = {
    { .id = "input_channels", .name = "Input Channels", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 8 },
      .min_i32 = 1, .max_i32 = 32,
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "output_channels", .name = "Output Channels", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32,
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "gain_db", .name = "Output Gain", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.0f },
      .min_f32 = -96.0f, .max_f32 = 24.0f, .unit = "dB",
      .update_policy = ORPHEUS_UPDATE_SMOOTHED,
      .readback = true, .persistent = true },
    { .id = "weights", .name = "Weight Matrix", .type = ORPHEUS_VALUE_STRING,
      .default_value = { .type = ORPHEUS_VALUE_STRING, .value.str = "1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0" },
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = false, .persistent = false }
};

static const OrpheusPort im3d_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "input_channels" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "output_channels" }
};

static const OrpheusComponentDescriptor im3d_descriptor = {
    .id = "orpheus.builtin.input_mixer_3d", .version = "1.0.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = im3d_ports, .port_count = 2, .params = im3d_params, .param_count = 4,
    .state_size = sizeof(InputMixer3DState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = false
};

static const OrpheusComponentDescriptor* im3d_get_descriptor(void) { return &im3d_descriptor; }

/* ---- 生命周期 ---- */

static int im3d_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(InputMixer3DState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}

static int im3d_destroy(void* state) { (void)state; return ORPHEUS_OK; }

static int im3d_prepare(void* state, const OrpheusConfig* config) {
    InputMixer3DState* s = (InputMixer3DState*)state;
    s->inputChannels  = (uint32_t)read_int(config, "input_channels", 8);
    s->outputChannels = (uint32_t)read_int(config, "output_channels", 2);
    if (s->inputChannels < 1)  s->inputChannels = 1;
    if (s->inputChannels > IM3D_MAX_CHANNELS)  s->inputChannels = IM3D_MAX_CHANNELS;
    if (s->outputChannels < 1) s->outputChannels = 1;
    if (s->outputChannels > IM3D_MAX_CHANNELS) s->outputChannels = IM3D_MAX_CHANNELS;

    s->gain_db     = read_float(config, "gain_db", 0.0f);
    s->gain_linear = db_to_linear(s->gain_db);

    /* 初始化权重为单位矩阵（直通） */
    memset(s->weights, 0, sizeof(s->weights));
    uint32_t minCh = s->inputChannels < s->outputChannels ? s->inputChannels : s->outputChannels;
    for (uint32_t i = 0; i < minCh; ++i) {
        s->weights[i * IM3D_MAX_CHANNELS + i] = 1.0f;
    }

    /* 解析权重字符串（如果提供） */
    const char* wstr = read_string(config, "weights", NULL);
    if (wstr != NULL) {
        char buf[4096];
        strncpy(buf, wstr, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        char* tok = strtok(buf, ",");
        uint32_t total = s->outputChannels * s->inputChannels;
        uint32_t idx = 0;
        while (tok != NULL && idx < total) {
            uint32_t row = idx / s->inputChannels;
            uint32_t col = idx % s->inputChannels;
            s->weights[row * IM3D_MAX_CHANNELS + col] = (float)atof(tok);
            idx++;
            tok = strtok(NULL, ",");
        }
    }
    return ORPHEUS_OK;
}

static int im3d_reset(void* state) {
    InputMixer3DState* s = (InputMixer3DState*)state;
    s->gain_linear = db_to_linear(s->gain_db);
    return ORPHEUS_OK;
}

/* ---- 实时处理 ---- */

static int im3d_process(void* state, const OrpheusProcessContext* ctx) {
    InputMixer3DState* s = (InputMixer3DState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (in == NULL || out == NULL) return ORPHEUS_ERR_INVALID_ARG;

    uint32_t frames = ctx->frame_count;
    uint32_t inCh = s->inputChannels;
    uint32_t outCh = s->outputChannels;
    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;
    const float* w = s->weights;  /* BULK 前缓冲 */
    float g = s->gain_linear;

    /* 矩阵混音: out[o] = sum_i(w[o*MAX + i] * in[i]) * g */
    for (uint32_t n = 0; n < frames; ++n) {
        for (uint32_t o = 0; o < outCh; ++o) {
            float sum = 0.0f;
            const float* wRow = &w[o * IM3D_MAX_CHANNELS];
            const float* inFrame = &in_data[n * inCh];
            for (uint32_t i = 0; i < inCh; ++i) {
                sum += wRow[i] * inFrame[i];
            }
            out_data[n * outCh + o] = sum * g;
        }
    }
    out->frame_count = frames;
    return ORPHEUS_OK;
}

/* ---- 参数 ---- */

static int im3d_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    InputMixer3DState* s = (InputMixer3DState*)state;
    if (strcmp(param_id, "gain_db") == 0) {
        if (value->type != ORPHEUS_VALUE_FLOAT && value->type != ORPHEUS_VALUE_INT)
            return ORPHEUS_ERR_INVALID_ARG;
        s->gain_db = (value->type == ORPHEUS_VALUE_FLOAT) ? value->value.f32 : (float)value->value.i32;
        s->gain_linear = db_to_linear(s->gain_db);
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int im3d_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    InputMixer3DState* s = (InputMixer3DState*)state;
    if (strcmp(param_id, "gain_db") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT; value->value.f32 = s->gain_db; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "input_channels") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->inputChannels; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "output_channels") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->outputChannels; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

/* ---- 槽注册 ---- */

static int im3d_register_slots(void* state, const OrpheusRegistry* reg) {
    InputMixer3DState* s = (InputMixer3DState*)state;
    /* BULK 权重矩阵：output × input 个 float，行优先存储 */
    OrpheusSlotInfo weights_slot = {
        .kind = ORPHEUS_SLOT_BULK, .key = "weights", .name = "混音权重矩阵",
        .type = ORPHEUS_VALUE_FLOAT,
        .offset = (size_t)((char*)&s->weights[0] - (char*)s),
        .size = sizeof(float), .count = IM3D_MAX_CHANNELS * IM3D_MAX_CHANNELS,
        .flags = ORPHEUS_SLOT_DOUBLE_BUFFERED
    };
    reg->add(reg->ctx, &weights_slot);
    ORPHEUS_REG_SLOT(reg, s, gain_db, ORPHEUS_SLOT_SETTING, "gain_db", "输出增益",
                     ORPHEUS_VALUE_FLOAT, .min_f32=-96.0f, .max_f32=24.0f, .unit="dB",
                     .update_policy=ORPHEUS_UPDATE_SMOOTHED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_DIRECT_WRITE);
    ORPHEUS_REG_SLOT(reg, s, inputChannels, ORPHEUS_SLOT_SETTING, "input_channels", "输入通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, outputChannels, ORPHEUS_SLOT_SETTING, "output_channels", "输出通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    return ORPHEUS_OK;
}

/* ---- 接口 ---- */

static const OrpheusComponentInterface im3d_interface = {
    .get_descriptor = im3d_get_descriptor, .create = im3d_create, .destroy = im3d_destroy,
    .prepare = im3d_prepare, .reset = im3d_reset, .process = im3d_process,
    .set_parameter = im3d_set_parameter, .get_parameter = im3d_get_parameter,
    .get_state_value = NULL, .register_slots = im3d_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &im3d_interface; }
