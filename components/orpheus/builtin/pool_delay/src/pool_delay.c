#include "orpheus_pool_delay.h"

#include <stdlib.h>
#include <string.h>

static const OrpheusParameter params[] = {
    { .id = "channels", .name = "通道数", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = POOL_DELAY_MAX_CHANNELS,
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "pool_size_samples", .name = "共享池样本容量", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 600 },
      .min_i32 = 1, .max_i32 = POOL_DELAY_MAX_SAMPLES,
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true },
    { .id = "delays_i32", .name = "各通道整数延迟", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 0 },
      .min_i32 = 0, .max_i32 = POOL_DELAY_MAX_SAMPLES,
      .update_policy = ORPHEUS_UPDATE_BLOCK_BOUNDARY,
      .readback = true, .persistent = true },
    { .id = "enabled", .name = "启用延迟", .type = ORPHEUS_VALUE_BOOL,
      .default_value = { .type = ORPHEUS_VALUE_BOOL, .value.b = true },
      .update_policy = ORPHEUS_UPDATE_BLOCK_BOUNDARY, .readback = true, .persistent = true }
};

static const OrpheusPort ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .is_variable = true, .channels_param = "channels" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor descriptor = {
    .id = "orpheus.builtin.pool_delay", .version = "1.1.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = ports, .port_count = 2, .params = params, .param_count = 4,
    .state_size = sizeof(PoolDelayState), .alignment = 8,
    .realtime_safe = true, .supports_inplace = false
};

static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

static int create(void** state, const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    if (config && config->state_block) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(PoolDelayState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}

static int destroy(void* state) {
    PoolDelayState* s = (PoolDelayState*)state;
    if (s) { free(s->buffer); s->buffer = NULL; }
    return ORPHEUS_OK;
}

static int prepare(void* state, const OrpheusConfig* config) {
    if (!state || !config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    PoolDelayState* s = (PoolDelayState*)state;
    int32_t channels = 2, capacity = 600;
    bool enabled = true;
    for (uint32_t i = 0; i < config->param_count; ++i) {
        const char* key = config->param_ids[i];
        if (!key) continue;
        if (strcmp(key, "enabled") == 0) {
            if (config->param_values[i].type != ORPHEUS_VALUE_BOOL) return ORPHEUS_ERR_INVALID_ARG;
            enabled = config->param_values[i].value.b;
            continue;
        }
        int32_t* target = strcmp(key, "channels") == 0 ? &channels
            : strcmp(key, "pool_size_samples") == 0 ? &capacity : NULL;
        if (!target) continue;
        if (config->param_values[i].type != ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        *target = config->param_values[i].value.i32;
    }
    if (channels < 1 || channels > POOL_DELAY_MAX_CHANNELS ||
        capacity < 1 || capacity > POOL_DELAY_MAX_SAMPLES) return ORPHEUS_ERR_INVALID_ARG;
    float* buffer = (float*)calloc((size_t)capacity, sizeof(float));
    if (!buffer) return ORPHEUS_ERR_OUT_OF_MEMORY;
    free(s->buffer);
    s->buffer = buffer;
    s->channels = (uint32_t)channels;
    s->pool_size = (uint32_t)capacity;
    s->cursor = 0;
    s->enabled = enabled;
    memset(s->delays_i32, 0, sizeof(s->delays_i32));
    return ORPHEUS_OK;
}

static int reset(void* state) {
    PoolDelayState* s = (PoolDelayState*)state;
    if (!s || !s->buffer) return ORPHEUS_ERR_INVALID_ARG;
    memset(s->buffer, 0, (size_t)s->pool_size * sizeof(float));
    s->cursor = 0;
    return ORPHEUS_OK;
}

static uint32_t advance(uint32_t position, uint32_t offset, uint32_t capacity) {
    return (uint32_t)(((uint64_t)position + offset) % capacity);
}

static bool overlap(const void* a, const void* b, size_t bytes) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x >= y ? x - y < bytes : y - x < bytes;
}

static int process(void* state, const OrpheusProcessContext* ctx) {
    PoolDelayState* s = (PoolDelayState*)state;
    if (!s || !s->buffer || !ctx || !ctx->inputs || !ctx->outputs ||
        ctx->input_count < 1 || ctx->output_count < 1 ||
        !ctx->inputs[0] || !ctx->outputs[0] ||
        !ctx->inputs[0]->data || !ctx->outputs[0]->data) return ORPHEUS_ERR_INVALID_ARG;
    const float* input = (const float*)ctx->inputs[0]->data;
    float* output = (float*)ctx->outputs[0]->data;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    const uint32_t frames = ctx->frame_count;
    size_t bytes = (size_t)frames * s->channels * sizeof(float);
    if (input == output || overlap(input, output, bytes) ||
        in->format != ORPHEUS_FORMAT_F32 || out->format != ORPHEUS_FORMAT_F32 ||
        !in->interleaved || !out->interleaved || in->channels != s->channels || out->channels != s->channels ||
        in->frame_capacity < frames || out->frame_capacity < frames || in->frame_count < frames)
        return ORPHEUS_ERR_INVALID_ARG;
    if (!s->enabled) {
        memcpy(output, input, bytes);
        out->frame_count = frames;
        return orpheus_process_next(ctx);
    }
    for (uint32_t c = 0; c < s->channels; ++c) {
        if (s->delays_i32[c] < 0 || (uint32_t)s->delays_i32[c] > s->pool_size)
            return ORPHEUS_ERR_INVALID_ARG;
    }
    uint32_t base = s->cursor;
    /* 全部通道先读旧池，再统一写入；变长不重排或清除旧历史。 */
    for (uint32_t c = 0; c < s->channels; ++c) {
        const uint32_t delay = (uint32_t)s->delays_i32[c];
        const uint32_t partial = delay < frames ? delay : frames;
        uint32_t index = advance(base, 1, s->pool_size);
        for (uint32_t n = 0; n < partial; ++n) {
            output[(size_t)n * s->channels + c] = s->buffer[index];
            index = advance(index, 1, s->pool_size);
        }
        for (uint32_t n = partial; n < frames; ++n)
            output[(size_t)n * s->channels + c] = input[(size_t)(n - partial) * s->channels + c];
        base = advance(base, delay, s->pool_size);
    }
    s->cursor = advance(s->cursor, frames, s->pool_size);
    base = s->cursor;
    for (uint32_t c = 0; c < s->channels; ++c) {
        const uint32_t delay = (uint32_t)s->delays_i32[c];
        const uint32_t partial = delay < frames ? delay : frames;
        uint32_t index = advance(base, delay - partial + 1, s->pool_size);
        for (uint32_t n = frames - partial; n < frames; ++n) {
            s->buffer[index] = input[(size_t)n * s->channels + c];
            index = advance(index, 1, s->pool_size);
        }
        base = advance(base, delay, s->pool_size);
    }
    ctx->outputs[0]->frame_count = frames;
    return orpheus_process_next(ctx);
}

static int set_parameter(void* state, const char* key, const OrpheusValue* value) {
    if (!state || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "enabled") == 0) {
        if (value->type != ORPHEUS_VALUE_BOOL) return ORPHEUS_ERR_INVALID_ARG;
        ((PoolDelayState*)state)->enabled = value->value.b;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_UNSUPPORTED;
}

static int get_parameter(void* state, const char* key, OrpheusValue* value) {
    PoolDelayState* s = (PoolDelayState*)state;
    if (!s || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "enabled") == 0) {
        value->type = ORPHEUS_VALUE_BOOL;
        value->value.b = s->enabled;
        return ORPHEUS_OK;
    }
    value->type = ORPHEUS_VALUE_INT;
    if (strcmp(key, "channels") == 0) value->value.i32 = (int32_t)s->channels;
    else if (strcmp(key, "pool_size_samples") == 0) value->value.i32 = (int32_t)s->pool_size;
    else return ORPHEUS_ERR_NOT_FOUND;
    return ORPHEUS_OK;
}

static int register_slots(void* state, const OrpheusRegistry* reg) {
    PoolDelayState* s = (PoolDelayState*)state;
    if (!s || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                    ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=POOL_DELAY_MAX_CHANNELS,
                    .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                    .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, pool_size, ORPHEUS_SLOT_SETTING, "pool_size_samples", "共享池样本容量",
                    ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=POOL_DELAY_MAX_SAMPLES,
                    .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                    .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, enabled, ORPHEUS_SLOT_SETTING, "enabled", "启用延迟",
                    ORPHEUS_VALUE_BOOL, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY,
                    .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_DIRECT_WRITE);
    OrpheusSlotInfo slot = {
        .kind = ORPHEUS_SLOT_BULK, .key = "delays_i32", .name = "各通道整数延迟",
        .type = ORPHEUS_VALUE_INT, .offset = offsetof(PoolDelayState, delays_i32),
        .size = sizeof(int32_t), .count = s->channels,
        .min_i32 = 0, .max_i32 = (int32_t)s->pool_size,
        .update_policy = ORPHEUS_UPDATE_BLOCK_BOUNDARY,
        .flags = ORPHEUS_SLOT_DOUBLE_BUFFERED | ORPHEUS_SLOT_READBACK
    };
    reg->add(reg->ctx, &slot);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface interface = {
    .get_descriptor = get_descriptor, .create = create, .destroy = destroy,
    .prepare = prepare, .reset = reset, .process = process,
    .set_parameter = set_parameter, .get_parameter = get_parameter,
    .register_slots = register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &interface; }
