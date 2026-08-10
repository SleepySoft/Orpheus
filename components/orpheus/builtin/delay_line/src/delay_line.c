#include "orpheus_delay_line.h"

#include <stdlib.h>
#include <string.h>

static int32_t read_int(const OrpheusConfig* config, const char* id, int32_t fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_INT)   return config->param_values[i].value.i32;
            if (config->param_values[i].type == ORPHEUS_VALUE_FLOAT) return (int32_t)config->param_values[i].value.f32;
        }
    }
    return fallback;
}

static const OrpheusParameter dl_params[] = {
    { .id = "channels", .name = "通道数", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32,
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "max_delay_samples", .name = "最大延迟样本数", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 4800 },
      .min_i32 = 1, .max_i32 = DELAY_LINE_MAX_DELAY,
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true },
    { .id = "delays_samples", .name = "每通道延迟样本数", .type = ORPHEUS_VALUE_STRING,
      .default_value = { .type = ORPHEUS_VALUE_STRING, .value.str = "0,0" },
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = false, .persistent = false }
};

static const OrpheusPort dl_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor dl_descriptor = {
    .id = "orpheus.builtin.delay_line", .version = "1.0.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = dl_ports, .port_count = 2, .params = dl_params, .param_count = 3,
    .state_size = sizeof(DelayLineState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = false
};

static const OrpheusComponentDescriptor* dl_get_descriptor(void) { return &dl_descriptor; }

static int dl_create(void** state, const OrpheusConfig* config) {
    (void)config;
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(DelayLineState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}

static int dl_destroy(void* state) {
    DelayLineState* s = (DelayLineState*)state;
    if (s && s->buffer) free(s->buffer);
    return ORPHEUS_OK;
}

static int dl_prepare(void* state, const OrpheusConfig* config) {
    DelayLineState* s = (DelayLineState*)state;
    s->channels = (uint32_t)read_int(config, "channels", 2);
    if (s->channels < 1) s->channels = 1;
    if (s->channels > DELAY_LINE_MAX_CHANNELS) s->channels = DELAY_LINE_MAX_CHANNELS;

    int32_t max_delay = read_int(config, "max_delay_samples", 4800);
    if (max_delay < 1) max_delay = 1;
    if (max_delay > DELAY_LINE_MAX_DELAY) max_delay = DELAY_LINE_MAX_DELAY;
    s->max_delay = (uint32_t)max_delay;

    /* 默认每通道延迟为 0 */
    for (uint32_t c = 0; c < DELAY_LINE_MAX_CHANNELS; ++c) s->delays_samples[c] = 0.0f;

    /* 解析 delays_samples 字符串 */
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], "delays_samples") == 0 &&
            config->param_values[i].type == ORPHEUS_VALUE_STRING) {
            const char* p = config->param_values[i].value.str;
            uint32_t idx = 0;
            while (p && *p && idx < s->channels) {
                char* end = NULL;
                float v = strtof(p, &end);
                if (end == p) { ++p; continue; }
                if (v < 0.0f) v = 0.0f;
                if (v > (float)s->max_delay) v = (float)s->max_delay;
                s->delays_samples[idx++] = v;
                p = end;
                while (*p == ',' || *p == ' ' || *p == '\t') ++p;
            }
            break;
        }
    }

    /* 容量：每通道 (max_delay + block_size + 1) 样本，避免每帧读写跨圈判断 */
    uint32_t block_size = config->block_size > 0 ? config->block_size : 32;
    s->capacity = (s->max_delay + block_size + 1) * s->channels;
    s->buffer = (float*)calloc(s->capacity, sizeof(float));
    if (!s->buffer) return ORPHEUS_ERR_OUT_OF_MEMORY;
    s->write_pos = 0;
    return ORPHEUS_OK;
}

static int dl_reset(void* state) {
    DelayLineState* s = (DelayLineState*)state;
    if (s && s->buffer) memset(s->buffer, 0, s->capacity * sizeof(float));
    if (s) s->write_pos = 0;
    return ORPHEUS_OK;
}

static int dl_process(void* state, const OrpheusProcessContext* ctx) {
    DelayLineState* s = (DelayLineState*)state;
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
            s->buffer[s->write_pos + c] = x;

            /* 读取延迟样本：当前写位置往回推 delay 个样本 */
            uint32_t delay = (uint32_t)(s->delays_samples[c] + 0.5f);
            uint32_t read_idx = (s->write_pos + s->capacity - delay * ch) % s->capacity;
            out_data[n * ch + c] = s->buffer[read_idx + c];
        }
        s->write_pos = (s->write_pos + ch) % s->capacity;
    }

    out->frame_count = frames;
    return ORPHEUS_OK;
}

static int dl_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    (void)state; (void)param_id; (void)value;
    return ORPHEUS_ERR_UNSUPPORTED;
}

static int dl_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    DelayLineState* s = (DelayLineState*)state;
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->channels; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "max_delay_samples") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->max_delay; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int dl_register_slots(void* state, const OrpheusRegistry* reg) {
    DelayLineState* s = (DelayLineState*)state;
    OrpheusSlotInfo delays_slot = {
        .kind = ORPHEUS_SLOT_BULK, .key = "delays_samples", .name = "每通道延迟样本数",
        .type = ORPHEUS_VALUE_FLOAT,
        .offset = (size_t)((char*)&s->delays_samples[0] - (char*)s),
        .size = sizeof(float), .count = s->channels,
        .flags = ORPHEUS_SLOT_DOUBLE_BUFFERED
    };
    reg->add(reg->ctx, &delays_slot);

    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, max_delay, ORPHEUS_SLOT_SETTING, "max_delay_samples", "最大延迟样本数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=DELAY_LINE_MAX_DELAY,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface dl_interface = {
    .get_descriptor = dl_get_descriptor, .create = dl_create, .destroy = dl_destroy,
    .prepare = dl_prepare, .reset = dl_reset, .process = dl_process,
    .set_parameter = dl_set_parameter, .get_parameter = dl_get_parameter,
    .get_state_value = NULL, .register_slots = dl_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &dl_interface; }
