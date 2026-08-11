#include "orpheus_circular_buffer.h"

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

static const OrpheusParameter cb_params[] = {
    { .id = "channels", .name = "通道数", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 64, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "frame_size", .name = "帧长", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 128 },
      .min_i32 = 2, .max_i32 = 4096, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "hop_size", .name = "帧移", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 64 },
      .min_i32 = 1, .max_i32 = 4096, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "num_frames", .name = "每块输出帧数", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 12 },
      .min_i32 = 1, .max_i32 = 256, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true }
};

static const OrpheusPort cb_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor cb_descriptor = {
    .id = "orpheus.builtin.circular_buffer", .version = "1.0.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = cb_ports, .port_count = 2, .params = cb_params, .param_count = 4,
    .state_size = sizeof(CircularBufferState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = false
};

static const OrpheusComponentDescriptor* cb_get_descriptor(void) { return &cb_descriptor; }

static void cb_free(CircularBufferState* s) {
    free(s->history);
    free(s->scratch);
    s->history = NULL;
    s->scratch = NULL;
}

static int cb_create(void** state, const OrpheusConfig* config) {
    (void)config;
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(CircularBufferState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}

static int cb_destroy(void* state) {
    cb_free((CircularBufferState*)state);
    return ORPHEUS_OK;
}

static int cb_prepare(void* state, const OrpheusConfig* config) {
    CircularBufferState* s = (CircularBufferState*)state;
    cb_free(s);

    s->channels = config->channels > 0 ? config->channels : 2;
    s->frame_size = (uint32_t)read_float(config, "frame_size", 128.0f);
    s->hop_size = (uint32_t)read_float(config, "hop_size", 64.0f);
    s->num_frames = (uint32_t)read_float(config, "num_frames", 12.0f);
    if (s->frame_size < 1) s->frame_size = 128;
    if (s->hop_size < 1) s->hop_size = 64;
    if (s->num_frames < 1) s->num_frames = 12;
    s->history_len = s->frame_size > s->hop_size ? s->frame_size - s->hop_size : 0;
    s->max_input_frames = config->block_size > 0 ? config->block_size : (s->num_frames * s->hop_size);

    s->history = (float*)calloc((size_t)s->channels * s->history_len, sizeof(float));
    s->scratch = (float*)calloc((size_t)s->channels * (s->history_len + s->max_input_frames), sizeof(float));
    if ((s->history_len > 0 && !s->history) || !s->scratch) {
        cb_free(s);
        return ORPHEUS_ERR_OUT_OF_MEMORY;
    }
    return ORPHEUS_OK;
}

static int cb_reset(void* state) {
    CircularBufferState* s = (CircularBufferState*)state;
    if (s->history && s->history_len > 0) {
        memset(s->history, 0, (size_t)s->channels * s->history_len * sizeof(float));
    }
    return ORPHEUS_OK;
}

static int cb_process(void* state, const OrpheusProcessContext* ctx) {
    CircularBufferState* s = (CircularBufferState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (in == NULL || out == NULL) return ORPHEUS_ERR_INVALID_ARG;

    uint32_t ch = s->channels;
    uint32_t frame = s->frame_size;
    uint32_t hop = s->hop_size;
    uint32_t nframes = s->num_frames;
    uint32_t hlen = s->history_len;
    uint32_t frames = ctx->frame_count;
    if (frames > s->max_input_frames) frames = s->max_input_frames;

    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;
    float* scratch = s->scratch;

    /* 组装历史 + 新输入到 scratch（交错格式） */
    for (uint32_t c = 0; c < ch; ++c) {
        for (uint32_t i = 0; i < hlen; ++i) {
            scratch[(i * ch) + c] = s->history[(i * ch) + c];
        }
        for (uint32_t i = 0; i < frames; ++i) {
            scratch[((hlen + i) * ch) + c] = in_data[(i * ch) + c];
        }
    }

    /* 提取 num_frames 个重叠帧，输出帧顺序展开 */
    uint32_t out_frames = nframes * frame;
    if (out->frame_capacity < out_frames) out_frames = out->frame_capacity;
    uint32_t actual_nframes = out_frames / frame;
    for (uint32_t c = 0; c < ch; ++c) {
        for (uint32_t f = 0; f < actual_nframes; ++f) {
            uint32_t start = f * hop;
            for (uint32_t n = 0; n < frame; ++n) {
                uint32_t src_idx = start + n;
                uint32_t dst_idx = (f * frame + n) * ch + c;
                out_data[dst_idx] = scratch[src_idx * ch + c];
            }
        }
    }

    /* 更新历史：保留组合流末尾 hlen 个样本 */
    if (hlen > 0) {
        for (uint32_t c = 0; c < ch; ++c) {
            for (uint32_t k = 0; k < hlen; ++k) {
                s->history[(k * ch) + c] = scratch[((frames + k) * ch) + c];
            }
        }
    }

    out->frame_count = actual_nframes * frame;
    return ORPHEUS_OK;
}

static int cb_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    CircularBufferState* s = (CircularBufferState*)state;
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->channels; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "frame_size") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->frame_size; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "hop_size") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->hop_size; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "num_frames") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->num_frames; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int cb_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    (void)state; (void)param_id; (void)value;
    return ORPHEUS_ERR_UNSUPPORTED;
}

static int cb_register_slots(void* state, const OrpheusRegistry* reg) {
    CircularBufferState* s = (CircularBufferState*)state;
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=64,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, frame_size, ORPHEUS_SLOT_SETTING, "frame_size", "帧长",
                     ORPHEUS_VALUE_INT, .min_i32=2, .max_i32=4096,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, hop_size, ORPHEUS_SLOT_SETTING, "hop_size", "帧移",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=4096,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, num_frames, ORPHEUS_SLOT_SETTING, "num_frames", "每块输出帧数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=256,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface cb_interface = {
    .get_descriptor = cb_get_descriptor, .create = cb_create, .destroy = cb_destroy,
    .prepare = cb_prepare, .reset = cb_reset, .process = cb_process,
    .set_parameter = cb_set_parameter, .get_parameter = cb_get_parameter,
    .get_state_value = NULL, .register_slots = cb_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &cb_interface; }
