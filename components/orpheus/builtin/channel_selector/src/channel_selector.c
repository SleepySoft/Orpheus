#include "orpheus_channel_selector.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define INT_PARAM(key, label, initial, low, high, signature) \
    {.id=key, .name=label, .type=ORPHEUS_VALUE_INT, \
     .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=initial}, .min_i32=low, .max_i32=high, \
     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .readback=true, .persistent=true, .affects_signature=signature}
static const OrpheusParameter params[] = {
    INT_PARAM("channels_a", "A输入通道数", 2, 1, 64, true),
    INT_PARAM("channels_b", "B输入通道数", 2, 1, 64, true),
    INT_PARAM("channels_out", "输出通道数", 2, 1, 64, true),
    INT_PARAM("index_base", "索引起点", 0, 0, 1, false),
    INT_PARAM("high_index_policy", "正越界策略", 0, 0, 1, false),
    {.id="apply_gain", .name="应用逐通道增益", .type=ORPHEUS_VALUE_BOOL,
     .default_value={.type=ORPHEUS_VALUE_BOOL, .value.b=false},
     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .readback=true, .persistent=true},
    {.id="indices", .name="初始通道索引", .type=ORPHEUS_VALUE_STRING,
     .default_value={.type=ORPHEUS_VALUE_STRING, .value.str=""},
     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .readback=true, .persistent=true},
    {.id="gains", .name="初始逐通道线性增益", .type=ORPHEUS_VALUE_STRING,
     .default_value={.type=ORPHEUS_VALUE_STRING, .value.str=""},
     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .readback=true, .persistent=true}
};
static const OrpheusPort ports[] = {
    {.id="a", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO,
     .sample_format=ORPHEUS_FORMAT_F32, .is_variable=true, .channels_param="channels_a"},
    {.id="b", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO,
     .sample_format=ORPHEUS_FORMAT_F32, .is_variable=true, .channels_param="channels_b"},
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO,
     .sample_format=ORPHEUS_FORMAT_F32, .is_variable=true, .channels_param="channels_out"}
};
static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.channel_selector", .version="1.0.0", .abi_version=ORPHEUS_ABI_VERSION,
    .ports=ports, .port_count=3, .params=params, .param_count=8,
    .state_size=sizeof(ChannelSelectorState), .alignment=8, .realtime_safe=true, .supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }
static int create(void** state, const OrpheusConfig* config) {
    if (!state || !config) return ORPHEUS_ERR_INVALID_ARG;
    if (config->state_block) {
        *state = config->state_block;
        ((ChannelSelectorState*)*state)->owns_state = false;
    } else {
        *state = calloc(1, sizeof(ChannelSelectorState));
        if (!*state) return ORPHEUS_ERR_OUT_OF_MEMORY;
        ((ChannelSelectorState*)*state)->owns_state = true;
    }
    return ORPHEUS_OK;
}
static int destroy(void* state) {
    if (state && ((ChannelSelectorState*)state)->owns_state) free(state);
    return ORPHEUS_OK;
}
static int parse_indices(const char* text, int32_t* values, uint32_t count) {
    for (uint32_t c = 0; c < count; ++c) {
        char* end;
        errno = 0;
        long value = strtol(text, &end, 10);
        if (end == text || errno == ERANGE || value < INT32_MIN || value > INT32_MAX)
            return ORPHEUS_ERR_INVALID_ARG;
        while (isspace((unsigned char)*end)) ++end;
        if (c+1 < count ? *end != ',' : *end != '\0') return ORPHEUS_ERR_INVALID_ARG;
        values[c] = (int32_t)value;
        text = c+1 < count ? end+1 : end;
    }
    return ORPHEUS_OK;
}
static int parse_gains(const char* text, float* values, uint32_t count) {
    for (uint32_t c = 0; c < count; ++c) {
        char* end;
        float value = strtof(text, &end);
        if (end == text || !isfinite(value)) return ORPHEUS_ERR_INVALID_ARG;
        while (isspace((unsigned char)*end)) ++end;
        if (c+1 < count ? *end != ',' : *end != '\0') return ORPHEUS_ERR_INVALID_ARG;
        values[c] = value;
        text = c+1 < count ? end+1 : end;
    }
    return ORPHEUS_OK;
}
static int prepare(void* state, const OrpheusConfig* config) {
    if (!state || !config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    ChannelSelectorState* s = (ChannelSelectorState*)state;
    ChannelSelectorState next = {.channels_a=2, .channels_b=2, .channels_out=2, .owns_state=s->owns_state};
    const char* indices = "";
    const char* gains = "";
    for (uint32_t i = 0; i < config->param_count; ++i) {
        const char* key = config->param_ids[i];
        const OrpheusValue* value = &config->param_values[i];
        if (!key) continue;
        if (strcmp(key, "indices") == 0 || strcmp(key, "gains") == 0) {
            if (value->type != ORPHEUS_VALUE_STRING || !value->value.str) return ORPHEUS_ERR_INVALID_ARG;
            if (strcmp(key, "indices") == 0) indices = value->value.str;
            else gains = value->value.str;
        } else if (strcmp(key, "apply_gain") == 0) {
            if (value->type != ORPHEUS_VALUE_BOOL) return ORPHEUS_ERR_INVALID_ARG;
            next.apply_gain = value->value.b;
        } else if (strcmp(key, "index_base") == 0 || strcmp(key, "high_index_policy") == 0) {
            if (value->type != ORPHEUS_VALUE_INT || value->value.i32 < 0 || value->value.i32 > 1)
                return ORPHEUS_ERR_INVALID_ARG;
            if (strcmp(key, "index_base") == 0) next.index_base = value->value.i32;
            else next.high_index_policy = value->value.i32;
        } else if (strcmp(key, "channels_a") == 0 || strcmp(key, "channels_b") == 0 || strcmp(key, "channels_out") == 0) {
            if (value->type != ORPHEUS_VALUE_INT || value->value.i32 < 1 || value->value.i32 > 64)
                return ORPHEUS_ERR_INVALID_ARG;
            if (strcmp(key, "channels_a") == 0) next.channels_a = (uint32_t)value->value.i32;
            else if (strcmp(key, "channels_b") == 0) next.channels_b = (uint32_t)value->value.i32;
            else next.channels_out = (uint32_t)value->value.i32;
        }
    }
    for (uint32_t c = 0; c < next.channels_out; ++c) {
        next.indices[c] = (int32_t)c+next.index_base;
        next.gains[c] = 1.0f;
    }
    if ((*indices && parse_indices(indices, next.indices, next.channels_out) != ORPHEUS_OK) ||
        (*gains && parse_gains(gains, next.gains, next.channels_out) != ORPHEUS_OK))
        return ORPHEUS_ERR_INVALID_ARG;
    *s = next;
    return ORPHEUS_OK;
}
static int reset(void* state) { return state ? ORPHEUS_OK : ORPHEUS_ERR_INVALID_ARG; }
static int process(void* state, const OrpheusProcessContext* ctx) {
    if (!state || !ctx || !ctx->inputs || !ctx->outputs || ctx->input_count < 2 || ctx->output_count < 1)
        return ORPHEUS_ERR_INVALID_ARG;
    ChannelSelectorState* s = (ChannelSelectorState*)state;
    const OrpheusBuffer* a = ctx->inputs[0];
    const OrpheusBuffer* b = ctx->inputs[1];
    OrpheusBuffer* out = ctx->outputs[0];
    uint32_t frames = ctx->frame_count;
    if (!s->channels_a || s->channels_a > 64 || !s->channels_b || s->channels_b > 64 ||
        !s->channels_out || s->channels_out > 64 || s->index_base < 0 || s->index_base > 1 ||
        s->high_index_policy < 0 || s->high_index_policy > 1 ||
        (a && (!a->data || a->channels != s->channels_a || a->frame_capacity < frames)) ||
        (b && (!b->data || b->channels != s->channels_b || b->frame_capacity < frames)) ||
        (out && (!out->data || out->channels != s->channels_out || out->frame_capacity < frames)))
        return ORPHEUS_ERR_INVALID_ARG;
    if (!out) return ORPHEUS_OK;
    float* destination = (float*)out->data;
    int64_t total = (int64_t)s->channels_a+s->channels_b;
    for (uint32_t c = 0; c < s->channels_out; ++c) {
        int64_t index = (int64_t)s->indices[c]-s->index_base;
        if (index >= total && s->high_index_policy) index = total-1;
        const OrpheusBuffer* input = index < 0 || index >= total ? NULL : index < s->channels_a ? a : b;
        uint32_t channel = index < s->channels_a ? (uint32_t)index : (uint32_t)(index-s->channels_a);
        for (uint32_t n = 0; n < frames; ++n) {
            float* output = &destination[n*s->channels_out+c];
            if (!input) *output = 0.0f;
            else {
                const float* sample = (const float*)input->data+n*input->channels+channel;
                if (s->apply_gain) *output = *sample*s->gains[c];
                else memcpy(output, sample, sizeof(float));
            }
        }
    }
    out->frame_count = frames;
    return ORPHEUS_OK;
}
static int set_parameter(void* state, const char* key, const OrpheusValue* value) {
    (void)state; (void)key; (void)value;
    return ORPHEUS_ERR_UNSUPPORTED;
}
static int get_parameter(void* state, const char* key, OrpheusValue* value) {
    if (!state || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    ChannelSelectorState* s = (ChannelSelectorState*)state;
    value->type = ORPHEUS_VALUE_INT;
    if (strcmp(key, "channels_a") == 0) value->value.i32 = (int32_t)s->channels_a;
    else if (strcmp(key, "channels_b") == 0) value->value.i32 = (int32_t)s->channels_b;
    else if (strcmp(key, "channels_out") == 0) value->value.i32 = (int32_t)s->channels_out;
    else if (strcmp(key, "index_base") == 0) value->value.i32 = s->index_base;
    else if (strcmp(key, "high_index_policy") == 0) value->value.i32 = s->high_index_policy;
    else if (strcmp(key, "apply_gain") == 0) {
        value->type = ORPHEUS_VALUE_BOOL; value->value.b = s->apply_gain;
    } else if (strcmp(key, "indices") == 0 || strcmp(key, "gains") == 0) return ORPHEUS_ERR_UNSUPPORTED;
    else return ORPHEUS_ERR_NOT_FOUND;
    return ORPHEUS_OK;
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    if (!state || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    ChannelSelectorState* s = (ChannelSelectorState*)state;
    OrpheusSlotInfo slot = {.kind=ORPHEUS_SLOT_BULK, .key="indices", .name="通道索引",
        .type=ORPHEUS_VALUE_INT, .offset=offsetof(ChannelSelectorState, indices),
        .size=sizeof(int32_t), .count=s->channels_out, .min_i32=INT32_MIN, .max_i32=INT32_MAX,
        .flags=ORPHEUS_SLOT_DOUBLE_BUFFERED};
    reg->add(reg->ctx, &slot);
    slot = (OrpheusSlotInfo){.kind=ORPHEUS_SLOT_BULK, .key="gains", .name="逐通道增益",
        .type=ORPHEUS_VALUE_FLOAT, .offset=offsetof(ChannelSelectorState, gains),
        .size=sizeof(float), .count=s->channels_out, .flags=ORPHEUS_SLOT_DOUBLE_BUFFERED};
    reg->add(reg->ctx, &slot);
#define REGISTER_INT(field, label, low, high, signature) \
    ORPHEUS_REG_SLOT(reg, s, field, ORPHEUS_SLOT_SETTING, #field, label, ORPHEUS_VALUE_INT, \
        .min_i32=low, .max_i32=high, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, \
        .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | (signature ? ORPHEUS_SLOT_AFFECTS_SIGNATURE : 0))
    REGISTER_INT(channels_a, "A输入通道数", 1, 64, true);
    REGISTER_INT(channels_b, "B输入通道数", 1, 64, true);
    REGISTER_INT(channels_out, "输出通道数", 1, 64, true);
    REGISTER_INT(index_base, "索引起点", 0, 1, false);
    REGISTER_INT(high_index_policy, "正越界策略", 0, 1, false);
#undef REGISTER_INT
    ORPHEUS_REG_SLOT(reg, s, apply_gain, ORPHEUS_SLOT_SETTING, "apply_gain", "应用逐通道增益",
        ORPHEUS_VALUE_BOOL, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
        .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    return ORPHEUS_OK;
}
static const OrpheusComponentInterface interface = {
    .get_descriptor=get_descriptor, .create=create, .destroy=destroy, .prepare=prepare, .reset=reset,
    .process=process, .set_parameter=set_parameter, .get_parameter=get_parameter, .register_slots=register_slots
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &interface; }
