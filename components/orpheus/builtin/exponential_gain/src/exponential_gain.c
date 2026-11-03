#include "orpheus_exponential_gain.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define FLOAT_PARAM(key, label, initial, policy) \
    {.id=key, .name=label, .type=ORPHEUS_VALUE_FLOAT, \
     .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=initial}, \
     .update_policy=policy, .readback=true, .persistent=true}

static const OrpheusParameter params[] = {
    {.id="channels", .name="通道数", .type=ORPHEUS_VALUE_INT,
     .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=2},
     .min_i32=1, .max_i32=64, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
     .readback=true, .persistent=true, .affects_signature=true},
    FLOAT_PARAM("target_gain", "目标增益", 1.0f, ORPHEUS_UPDATE_BLOCK_BOUNDARY),
    FLOAT_PARAM("ramp_coefficient", "每样本指数乘数", 1.0f, ORPHEUS_UPDATE_BLOCK_BOUNDARY),
    {.id="remaining_blocks", .name="剩余调用块数", .type=ORPHEUS_VALUE_FLOAT,
     .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.0f},
     .min_f32=0.0f, .max_f32=16777216.0f, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY,
     .readback=true, .persistent=true},
    FLOAT_PARAM("initial_gain", "初始增益", 1.0f, ORPHEUS_UPDATE_RESTART_REQUIRED),
    FLOAT_PARAM("min_gain", "指数起点下限", 4.7315126e-7f, ORPHEUS_UPDATE_RESTART_REQUIRED),
    FLOAT_PARAM("snap_threshold", "目标吸附阈值", 1.0e-9f, ORPHEUS_UPDATE_RESTART_REQUIRED)
};
static const OrpheusPort ports[] = {
    {.id="in", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO,
     .sample_format=ORPHEUS_FORMAT_F32, .is_variable=true, .channels_param="channels"},
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO,
     .sample_format=ORPHEUS_FORMAT_F32, .is_variable=true, .channels_param="channels"}
};
static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.exponential_gain", .version="1.0.0",
    .abi_version=ORPHEUS_ABI_VERSION, .ports=ports, .port_count=2, .params=params, .param_count=7,
    .state_size=sizeof(ExponentialGainState), .alignment=8, .realtime_safe=true, .supports_inplace=true
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

static int create(void** state, const OrpheusConfig* config) {
    if (!state || !config) return ORPHEUS_ERR_INVALID_ARG;
    if (config->state_block) {
        *state = config->state_block;
        ((ExponentialGainState*)*state)->owns_state = false;
    } else {
        *state = calloc(1, sizeof(ExponentialGainState));
        if (!*state) return ORPHEUS_ERR_OUT_OF_MEMORY;
        ((ExponentialGainState*)*state)->owns_state = true;
    }
    return ORPHEUS_OK;
}
static int destroy(void* state) {
    if (state && ((ExponentialGainState*)state)->owns_state) free(state);
    return ORPHEUS_OK;
}
static int valid(const ExponentialGainState* s) {
    return s->channels >= 1 && s->channels <= 64 &&
        isfinite(s->target_gain) && s->target_gain >= 0.0f &&
        isfinite(s->ramp_coefficient) && s->ramp_coefficient >= 0.0f &&
        isfinite(s->remaining_blocks) && s->remaining_blocks >= 0.0f &&
        s->remaining_blocks <= 16777216.0f && floorf(s->remaining_blocks) == s->remaining_blocks &&
        isfinite(s->initial_gain) && s->initial_gain >= 0.0f &&
        isfinite(s->min_gain) && s->min_gain > 0.0f &&
        isfinite(s->snap_threshold) && s->snap_threshold >= 0.0f;
}
static int prepare(void* state, const OrpheusConfig* config) {
    if (!state || !config) return ORPHEUS_ERR_INVALID_ARG;
    ExponentialGainState* s = (ExponentialGainState*)state;
    ExponentialGainState next = {.channels=config->channels ? config->channels : 2,
        .target_gain=1.0f, .ramp_coefficient=1.0f, .initial_gain=1.0f,
        .min_gain=4.7315126e-7f, .snap_threshold=1.0e-9f, .load_initial=true,
        .owns_state=s->owns_state};
    for (uint32_t i = 0; i < config->param_count; ++i) {
        const char* id = config->param_ids[i];
        const OrpheusValue* v = &config->param_values[i];
        if (!id) continue;
        if (strcmp(id, "channels") == 0) {
            if (v->type != ORPHEUS_VALUE_INT || v->value.i32 < 1 || v->value.i32 > 64)
                return ORPHEUS_ERR_INVALID_ARG;
            next.channels = (uint32_t)v->value.i32;
        } else {
            float* field = NULL;
            if (strcmp(id, "target_gain") == 0) field = &next.target_gain;
            else if (strcmp(id, "ramp_coefficient") == 0) field = &next.ramp_coefficient;
            else if (strcmp(id, "remaining_blocks") == 0) field = &next.remaining_blocks;
            else if (strcmp(id, "initial_gain") == 0) field = &next.initial_gain;
            else if (strcmp(id, "min_gain") == 0) field = &next.min_gain;
            else if (strcmp(id, "snap_threshold") == 0) field = &next.snap_threshold;
            if (field) {
                if (v->type != ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
                *field = v->value.f32;
            }
        }
    }
    if (!valid(&next)) return ORPHEUS_ERR_INVALID_ARG;
    next.current_gain = next.initial_gain;
    *s = next;
    return ORPHEUS_OK;
}
static int reset(void* state) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    ExponentialGainState* s = (ExponentialGainState*)state;
    s->current_gain = s->initial_gain;
    s->delayed_target = s->filter_state = s->remaining_blocks = 0.0f;
    s->load_initial = true;
    return ORPHEUS_OK;
}
static int process(void* state, const OrpheusProcessContext* ctx) {
    if (!state || !ctx || !ctx->inputs || !ctx->outputs ||
        ctx->input_count < 1 || ctx->output_count < 1) return ORPHEUS_ERR_INVALID_ARG;
    ExponentialGainState* s = (ExponentialGainState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    uint32_t frames = ctx->frame_count;
    if (!valid(s) ||
        (in && (!in->data || in->channels != s->channels || in->frame_capacity < frames)) ||
        (out && (!out->data || out->channels != s->channels || out->frame_capacity < frames)))
        return ORPHEUS_ERR_INVALID_ARG;
    if (!frames) return ORPHEUS_OK;
    float current = fmaxf(s->current_gain, s->min_gain);
    bool ramping = fabsf(s->target_gain-current) > s->snap_threshold && s->remaining_blocks > 0.0f;
    if (ramping) {
        s->remaining_blocks -= 1.0f;
        if (s->load_initial || s->target_gain-s->delayed_target != 0.0f)
            s->filter_state = s->ramp_coefficient*current;
    }
    const float* input = in ? (const float*)in->data : NULL;
    float* output = out ? (float*)out->data : NULL;
    for (uint32_t n = 0; n < frames; ++n) {
        float gain = ramping ? s->filter_state : s->target_gain;
        if (ramping) s->filter_state = 0.0f - (-s->ramp_coefficient)*gain;
        if (output)
            for (uint32_t c = 0; c < s->channels; ++c)
                output[n*s->channels+c] = (input ? input[n*s->channels+c] : 0.0f)*gain;
        s->current_gain = gain;
    }
    if (ramping) {
        s->delayed_target = s->target_gain;
        s->load_initial = false;
    }
    if (out) out->frame_count = frames;
    return ORPHEUS_OK;
}
static int set_parameter(void* state, const char* key, const OrpheusValue* value) {
    if (!state || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    ExponentialGainState* s = (ExponentialGainState*)state;
    float* field;
    if (strcmp(key, "target_gain") == 0) field = &s->target_gain;
    else if (strcmp(key, "ramp_coefficient") == 0) field = &s->ramp_coefficient;
    else if (strcmp(key, "remaining_blocks") == 0) field = &s->remaining_blocks;
    else return ORPHEUS_ERR_NOT_FOUND;
    if (value->type != ORPHEUS_VALUE_FLOAT || !isfinite(value->value.f32) ||
        value->value.f32 < 0.0f ||
        (field == &s->remaining_blocks &&
         (value->value.f32 > 16777216.0f || floorf(value->value.f32) != value->value.f32)))
        return ORPHEUS_ERR_INVALID_ARG;
    *field = value->value.f32;
    return ORPHEUS_OK;
}
static int get_parameter(void* state, const char* key, OrpheusValue* value) {
    if (!state || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    ExponentialGainState* s = (ExponentialGainState*)state;
    value->type = ORPHEUS_VALUE_FLOAT;
    if (strcmp(key, "target_gain") == 0) value->value.f32 = s->target_gain;
    else if (strcmp(key, "ramp_coefficient") == 0) value->value.f32 = s->ramp_coefficient;
    else if (strcmp(key, "remaining_blocks") == 0) value->value.f32 = s->remaining_blocks;
    else if (strcmp(key, "initial_gain") == 0) value->value.f32 = s->initial_gain;
    else if (strcmp(key, "min_gain") == 0) value->value.f32 = s->min_gain;
    else if (strcmp(key, "snap_threshold") == 0) value->value.f32 = s->snap_threshold;
    else if (strcmp(key, "current_gain") == 0) value->value.f32 = s->current_gain;
    else if (strcmp(key, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->channels;
    } else return ORPHEUS_ERR_NOT_FOUND;
    return ORPHEUS_OK;
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    if (!state || !reg) return ORPHEUS_ERR_INVALID_ARG;
    ExponentialGainState* s = (ExponentialGainState*)state;
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
        ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=64, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
        .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
#define REGISTER(field, label, policy) \
    ORPHEUS_REG_SLOT(reg, s, field, ORPHEUS_SLOT_SETTING, #field, label, ORPHEUS_VALUE_FLOAT, \
        .update_policy=policy, .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK)
    REGISTER(target_gain, "目标增益", ORPHEUS_UPDATE_BLOCK_BOUNDARY);
    REGISTER(ramp_coefficient, "每样本指数乘数", ORPHEUS_UPDATE_BLOCK_BOUNDARY);
    ORPHEUS_REG_SLOT(reg, s, remaining_blocks, ORPHEUS_SLOT_SETTING, "remaining_blocks", "剩余调用块数",
        ORPHEUS_VALUE_FLOAT, .min_f32=0.0f, .max_f32=16777216.0f,
        .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    REGISTER(initial_gain, "初始增益", ORPHEUS_UPDATE_RESTART_REQUIRED);
    REGISTER(min_gain, "指数起点下限", ORPHEUS_UPDATE_RESTART_REQUIRED);
    REGISTER(snap_threshold, "目标吸附阈值", ORPHEUS_UPDATE_RESTART_REQUIRED);
#undef REGISTER
    ORPHEUS_REG_SLOT(reg, s, current_gain, ORPHEUS_SLOT_PROBE, "current_gain", "当前增益",
        ORPHEUS_VALUE_FLOAT, .flags=ORPHEUS_SLOT_READBACK);
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
