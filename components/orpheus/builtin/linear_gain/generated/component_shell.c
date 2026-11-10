#include "component_state.h"
#include <string.h>
#include <math.h>

static const OrpheusPort ports[] = {
    {.id="in", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
};

static const OrpheusParameter params[] = {
    {.id="gain", .name="线性增益", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=1.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="channels", .name="通道数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=2}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=1, .max_i32=32},
};

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.linear_gain", .version="1.0.0", .abi_version=7,
    .ports=ports, .port_count=2,
    .params=params, .param_count=2,
    .state_size=sizeof(orpheus_builtin_linear_gain_State), .alignment=_Alignof(orpheus_builtin_linear_gain_State), .realtime_safe=true
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

static int set_parameter(void* state, const char* key, const OrpheusValue* value) {
    orpheus_builtin_linear_gain_State* s = (orpheus_builtin_linear_gain_State*)state;
    if (!s || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "gain") == 0) {
        if (value->type != ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        s->gain = value->value.f32; return ORPHEUS_OK;
    }
    if (strcmp(key, "channels") == 0) {
        if (value->type != ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32 < 1 || value->value.i32 > 32) return ORPHEUS_ERR_INVALID_ARG;
        s->channels = value->value.i32; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int get_parameter(void* state, const char* key, OrpheusValue* value) {
    orpheus_builtin_linear_gain_State* s = (orpheus_builtin_linear_gain_State*)state;
    if (!s || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "gain") == 0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->gain; return ORPHEUS_OK;
    }
    if (strcmp(key, "channels") == 0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->channels; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int prepare(void* state, const OrpheusConfig* config) {
    orpheus_builtin_linear_gain_State* s = (orpheus_builtin_linear_gain_State*)state;
    if (!s || !config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_linear_gain_State candidate = *s;
    for (uint32_t i=0; i<config->param_count; ++i) {
        int result=set_parameter(&candidate, config->param_ids[i], &config->param_values[i]);
        if (result != ORPHEUS_OK) return result;
    }
    *s=candidate; return ORPHEUS_OK;
}
static int create(void** state, const OrpheusConfig* config) {
    if (!state || !config || !config->state_block) return ORPHEUS_ERR_INVALID_ARG;
    *state=NULL;
    if ((uintptr_t)config->state_block % _Alignof(orpheus_builtin_linear_gain_State)) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_linear_gain_State* s=(orpheus_builtin_linear_gain_State*)config->state_block;
    memset(s, 0, sizeof(*s));
    s->gain=1.000000000e+00f;
    s->channels=2;
    int result=prepare(s, config);
    if (result == ORPHEUS_OK) *state=s;
    return result;
}
static int destroy(void* state) { return state ? ORPHEUS_OK : ORPHEUS_ERR_INVALID_ARG; }
static int reset(void* state) {
    orpheus_builtin_linear_gain_State* s=(orpheus_builtin_linear_gain_State*)state;
    if (!s) return ORPHEUS_ERR_INVALID_ARG;
    memset(&s->algorithm, 0, sizeof(s->algorithm)); return ORPHEUS_OK;
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    orpheus_builtin_linear_gain_State* s=(orpheus_builtin_linear_gain_State*)state;
    if (!s || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_SLOT(reg, s, gain, ORPHEUS_SLOT_SETTING, "gain", "线性增益", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT) == ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=1, .max_i32=32) == ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    return ORPHEUS_OK;
}
static int valid_buffer(const OrpheusBuffer* buffer, uint32_t channels) {
    return !buffer || (buffer->format == ORPHEUS_FORMAT_F32 && buffer->interleaved
        && buffer->channels == channels && channels > 0
        && buffer->frame_count <= buffer->frame_capacity
        && (!buffer->frame_capacity || buffer->data)
        && (!buffer->data || (uintptr_t)buffer->data % _Alignof(float) == 0)
        && (size_t)buffer->frame_capacity <= SIZE_MAX / sizeof(float) / channels);
}
static int process(void* state, const OrpheusProcessContext* ctx) {
    orpheus_builtin_linear_gain_State* s=(orpheus_builtin_linear_gain_State*)state;
    orpheus_builtin_linear_gain_IO io={0};
    if (!s || !ctx) return ORPHEUS_ERR_INVALID_ARG;
    io.in.buffer=(ctx->inputs && ctx->input_count > 0) ? ctx->inputs[0] : NULL;
    if (!valid_buffer(io.in.buffer, (uint32_t)(s->channels)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.in.samples=io.in.buffer ? (const float*)io.in.buffer->data : NULL;
    io.out.buffer=(ctx->outputs && ctx->output_count > 0) ? ctx->outputs[0] : NULL;
    if (!valid_buffer(io.out.buffer, (uint32_t)(s->channels)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.out.samples=io.out.buffer ? (float*)io.out.buffer->data : NULL;
    if (io.out.buffer) {
        if (ctx->frame_count > io.out.buffer->frame_capacity) return ORPHEUS_ERR_INVALID_ARG;
        io.out.buffer->frame_count=ctx->frame_count;
    }
    io.out.delivery=(OrpheusDelivery){.buffer=io.out.buffer, .frame_index=ctx->frame_index,
        .epoch=ctx->epoch, .timeline_flags=ctx->timeline_flags};
    return orpheus_builtin_linear_gain_algorithm(s, &io, ctx);
}
static const OrpheusComponentInterface interface = {
    .get_descriptor=get_descriptor, .create=create, .destroy=destroy,
    .prepare=prepare, .reset=reset, .process=process,
    .set_parameter=set_parameter, .get_parameter=get_parameter, .register_slots=register_slots
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &interface; }
