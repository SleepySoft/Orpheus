#include "component_state.h"
#include <string.h>
#include <math.h>

static const OrpheusPort ports[] = {
    {.id="in", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
};

static const OrpheusParameter params[] = {
    {.id="inputs", .name="输入路数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=2}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=1, .max_i32=32},
    {.id="channels", .name="通道数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=2}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=1, .max_i32=64},
    {.id="select", .name="选择输入（1起，按实际路数钳位）", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=1}, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="select_index", .name="选择输入索引（0起，配置负值使用select）", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=-1}, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="ramp_ms", .name="每相邻一路淡化时长", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=2.000000000e+01f}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="ms", .readback=true, .persistent=true, .affects_signature=false, .min_f32=0.000000000e+00f, .max_f32=1.000000000e+03f},
    {.id="select_pos", .name="当前选择位置（0起）", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .readback=true, .persistent=true, .affects_signature=false},
};

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.n_way_mux", .version="1.2.0", .abi_version=7,
    .ports=ports, .port_count=2,
    .params=params, .param_count=6,
    .state_size=sizeof(orpheus_builtin_n_way_mux_State), .alignment=_Alignof(orpheus_builtin_n_way_mux_State),
    .scratch_size=0, .latency_samples=0,
    .realtime_safe=true, .supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

static void snapshot_parameters(const orpheus_builtin_n_way_mux_State* s, orpheus_builtin_n_way_mux_Parameters* p) {
    (void)s; memset(p, 0, sizeof(*p));
    p->inputs=s->inputs;
    p->channels=s->channels;
    p->select=s->select;
    p->select_index=s->select_index;
    p->ramp_ms=s->ramp_ms;
    p->select_pos=s->select_pos;
}
static void commit_parameters(orpheus_builtin_n_way_mux_State* s, const orpheus_builtin_n_way_mux_Parameters* p) {
    (void)s; (void)p;
    s->inputs=p->inputs;
    s->channels=p->channels;
    s->select=p->select;
    s->select_index=p->select_index;
    s->ramp_ms=p->ramp_ms;
    s->select_pos=p->select_pos;
}
static void default_parameters(orpheus_builtin_n_way_mux_Parameters* p) {
    memset(p, 0, sizeof(*p));
    p->inputs=2;
    p->channels=2;
    p->select=1;
    p->select_index=-1;
    p->ramp_ms=2.000000000e+01f;
    p->select_pos=0.000000000e+00f;
}
static int update_parameter(orpheus_builtin_n_way_mux_Parameters* p, const char* key, const OrpheusValue* value) {
    if (!p || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "inputs") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>32) return ORPHEUS_ERR_INVALID_ARG;
        p->inputs=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "channels") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>64) return ORPHEUS_ERR_INVALID_ARG;
        p->channels=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "select") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        p->select=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "select_index") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        p->select_index=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp_ms") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<0.000000000e+00f || value->value.f32>1.000000000e+03f) return ORPHEUS_ERR_INVALID_ARG;
        p->ramp_ms=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "select_pos") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->select_pos=value->value.f32;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int normalize_parameters(orpheus_builtin_n_way_mux_Parameters* p, const char* key) {
    int result=orpheus_builtin_n_way_mux_normalize(p, key);
    if (result!=ORPHEUS_OK) return result;
    {
        OrpheusValue value={.type=ORPHEUS_VALUE_INT,.value.i32=p->inputs};
        result=update_parameter(p, "inputs", &value);
        if (result!=ORPHEUS_OK) return result;
    }
    {
        OrpheusValue value={.type=ORPHEUS_VALUE_INT,.value.i32=p->channels};
        result=update_parameter(p, "channels", &value);
        if (result!=ORPHEUS_OK) return result;
    }
    {
        OrpheusValue value={.type=ORPHEUS_VALUE_INT,.value.i32=p->select};
        result=update_parameter(p, "select", &value);
        if (result!=ORPHEUS_OK) return result;
    }
    {
        OrpheusValue value={.type=ORPHEUS_VALUE_INT,.value.i32=p->select_index};
        result=update_parameter(p, "select_index", &value);
        if (result!=ORPHEUS_OK) return result;
    }
    {
        OrpheusValue value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=p->ramp_ms};
        result=update_parameter(p, "ramp_ms", &value);
        if (result!=ORPHEUS_OK) return result;
    }
    {
        OrpheusValue value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=p->select_pos};
        result=update_parameter(p, "select_pos", &value);
        if (result!=ORPHEUS_OK) return result;
    }
    return ORPHEUS_OK;
}
static int set_parameter(void* state, const char* key, const OrpheusValue* value) {
    orpheus_builtin_n_way_mux_State* s=(orpheus_builtin_n_way_mux_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "select_pos")==0) return ORPHEUS_ERR_UNSUPPORTED;
    orpheus_builtin_n_way_mux_Parameters before, candidate; snapshot_parameters(s, &before); candidate=before;
    int result=update_parameter(&candidate, key, value);
    if (result!=ORPHEUS_OK) return result;
    result=normalize_parameters(&candidate, key); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate); return ORPHEUS_OK;
}
static int get_parameter(void* state, const char* key, OrpheusValue* value) {
    orpheus_builtin_n_way_mux_State* s=(orpheus_builtin_n_way_mux_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "inputs")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->inputs; return ORPHEUS_OK;
    }
    if (strcmp(key, "channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->channels; return ORPHEUS_OK;
    }
    if (strcmp(key, "select")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->select; return ORPHEUS_OK;
    }
    if (strcmp(key, "select_index")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->select_index; return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp_ms")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->ramp_ms; return ORPHEUS_OK;
    }
    if (strcmp(key, "select_pos")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->select_pos; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int configure_parameters(orpheus_builtin_n_way_mux_Parameters* p, const OrpheusConfig* config) {
    if (!config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    default_parameters(p);
    bool explicit_channels=false;
    for (uint32_t i=0; i<config->param_count; ++i)
        if (config->param_ids[i] && strcmp(config->param_ids[i], "channels")==0) explicit_channels=true;
    if (!explicit_channels && config->channels) {
        if (config->channels>INT32_MAX) return ORPHEUS_ERR_INVALID_ARG;
        OrpheusValue channels={.type=ORPHEUS_VALUE_INT,.value.i32=(int32_t)config->channels};
        int result=update_parameter(p, "channels", &channels);
        if (result!=ORPHEUS_OK) return result;
    }
    for (uint32_t i=0; i<config->param_count; ++i) {
        int result=update_parameter(p, config->param_ids[i], &config->param_values[i]);
        if (result!=ORPHEUS_OK) return result;
    }
    int normalized=normalize_parameters(p, NULL); if (normalized!=ORPHEUS_OK) return normalized;
    return ORPHEUS_OK;
}
static int cleanup(orpheus_builtin_n_way_mux_State* s) {
    int result=ORPHEUS_OK;
    if (s->_orpheus_cleanup_pending) {
        s->_orpheus_cleanup_pending=false;
        memset(&s->algorithm, 0, sizeof(s->algorithm));
    }
    return result;
}
static int prepare(void* state, const OrpheusConfig* config) {
    orpheus_builtin_n_way_mux_State* s=(orpheus_builtin_n_way_mux_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_n_way_mux_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    result=cleanup(s); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate);
    s->_orpheus_cleanup_pending=true;
    result=orpheus_builtin_n_way_mux_prepare(s, config);
    if (result!=ORPHEUS_OK) { (void)cleanup(s); return result; }
    return ORPHEUS_OK;
}
static int create(void** state, const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=NULL;
    if (!config || !config->state_block) return ORPHEUS_ERR_INVALID_ARG;
    if ((uintptr_t)config->state_block % descriptor.alignment) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_n_way_mux_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    orpheus_builtin_n_way_mux_State* s=(orpheus_builtin_n_way_mux_State*)config->state_block;
    memset(s, 0, sizeof(*s)); commit_parameters(s, &candidate);
    s->_orpheus_alive=true; *state=s; return ORPHEUS_OK;
}
static int destroy(void* state) {
    orpheus_builtin_n_way_mux_State* s=(orpheus_builtin_n_way_mux_State*)state;
    if (!s) return ORPHEUS_ERR_INVALID_ARG;
    if (!s->_orpheus_alive) return ORPHEUS_OK;
    int result=cleanup(s); s->_orpheus_alive=false; return result;
}
static int reset(void* state) {
    orpheus_builtin_n_way_mux_State* s=(orpheus_builtin_n_way_mux_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_builtin_n_way_mux_reset(s);
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    orpheus_builtin_n_way_mux_State* s=(orpheus_builtin_n_way_mux_State*)state;
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_SLOT(reg, s, inputs, ORPHEUS_SLOT_SETTING, "inputs", "输入路数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=1, .max_i32=32)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=1, .max_i32=64)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, select, ORPHEUS_SLOT_SETTING, "select", "选择输入（1起，按实际路数钳位）", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, select_index, ORPHEUS_SLOT_SETTING, "select_index", "选择输入索引（0起，配置负值使用select）", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, ramp_ms, ORPHEUS_SLOT_SETTING, "ramp_ms", "每相邻一路淡化时长", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="ms", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=0.000000000e+00f, .max_f32=1.000000000e+03f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, select_pos, ORPHEUS_SLOT_PROBE, "select_pos", "当前选择位置（0起）", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    return ORPHEUS_OK;
}
static int valid_buffer(const OrpheusBuffer* buffer, uint32_t channels,
        OrpheusSampleFormat format, size_t sample_size, size_t alignment) {
    return !buffer || (buffer->format == format && buffer->interleaved
        && buffer->channels == channels && channels > 0
        && buffer->frame_count <= buffer->frame_capacity
        && (!buffer->frame_capacity || buffer->data)
        && (!buffer->data || (uintptr_t)buffer->data % alignment == 0)
        && (size_t)buffer->frame_capacity <= SIZE_MAX / sample_size / channels);
}
static int process(void* state, const OrpheusProcessContext* ctx) {
    orpheus_builtin_n_way_mux_State* s=(orpheus_builtin_n_way_mux_State*)state;
    orpheus_builtin_n_way_mux_IO io={0};
    if (!s || !s->_orpheus_alive || !ctx) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t _orpheus_input_count=0, _orpheus_output_count=0;
    int64_t _orpheus_count_in=s->inputs;
    if (_orpheus_count_in < 1 || _orpheus_count_in > 32)
        return ORPHEUS_ERR_INVALID_ARG;
    io.in_count=(uint32_t)_orpheus_count_in;
    _orpheus_input_count+=io.in_count;
    _orpheus_output_count+=1;
    if (ctx->input_count!=_orpheus_input_count || ctx->output_count!=_orpheus_output_count)
        return ORPHEUS_ERR_INVALID_ARG;
    if (ctx->trigger_input!=UINT32_MAX && ctx->trigger_input>=_orpheus_input_count)
        return ORPHEUS_ERR_INVALID_ARG;
    if (ctx->delivery && ctx->trigger_input==UINT32_MAX) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t _orpheus_input_slot=0;
    for (uint32_t _orpheus_i=0; _orpheus_i<io.in_count; ++_orpheus_i) {
    io.in[_orpheus_i].input_index=_orpheus_input_slot+_orpheus_i;
    io.in[_orpheus_i].buffer=(ctx->inputs && ctx->input_count > (_orpheus_input_slot+_orpheus_i)) ? ctx->inputs[_orpheus_input_slot+_orpheus_i] : NULL;
    if (!valid_buffer(io.in[_orpheus_i].buffer, (uint32_t)(s->channels),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.in[_orpheus_i].samples=io.in[_orpheus_i].buffer ? (const float*)io.in[_orpheus_i].buffer->data : NULL;
    }
    _orpheus_input_slot+=io.in_count;
    uint32_t _orpheus_output_slot=0;
    io.out.active=true;
    io.out.drive_port_no=1u;
    io.out.buffer=(ctx->outputs && ctx->output_count > (_orpheus_output_slot)) ? ctx->outputs[_orpheus_output_slot] : NULL;
    if (!valid_buffer(io.out.buffer, (uint32_t)(s->channels),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.out.samples=io.out.buffer ? (float*)io.out.buffer->data : NULL;
    if (io.out.buffer) {
        if (ctx->frame_count > io.out.buffer->frame_capacity) return ORPHEUS_ERR_INVALID_ARG;
        io.out.buffer->frame_count=ctx->frame_count;
    }
    io.out.delivery=(OrpheusDelivery){.buffer=io.out.buffer, .frame_index=ctx->frame_index,
        .epoch=ctx->epoch, .timeline_flags=ctx->timeline_flags};
    _orpheus_output_slot+=1;
    return orpheus_builtin_n_way_mux_algorithm(s, &io, ctx);
}
static const OrpheusComponentInterface interface = {
    .get_descriptor=get_descriptor, .create=create, .destroy=destroy,
    .prepare=prepare, .reset=reset, .process=process,
    .set_parameter=set_parameter, .get_parameter=get_parameter, .register_slots=register_slots,
    .hook=NULL
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &interface; }
