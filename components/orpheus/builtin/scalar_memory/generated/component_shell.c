#include "component_state.h"
#include <string.h>
#include <math.h>

static const OrpheusPort ports[] = {
    {.id="in", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_BULK, .sample_format=ORPHEUS_FORMAT_F32, .channels=1, .sample_rate=0, .block_size=1, .is_variable=false, .channels_param=NULL},
    {.id="update", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_BULK, .sample_format=ORPHEUS_FORMAT_I32, .channels=1, .sample_rate=0, .block_size=1, .is_variable=false, .channels_param=NULL},
    {.id="initial", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_BULK, .sample_format=ORPHEUS_FORMAT_F32, .channels=1, .sample_rate=0, .block_size=1, .is_variable=false, .channels_param=NULL},
    {.id="tick", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_TRIGGER, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=false, .channels_param=NULL},
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_BULK, .sample_format=ORPHEUS_FORMAT_F32, .channels=1, .sample_rate=0, .block_size=1, .is_variable=false, .channels_param=NULL},
    {.id="enabled", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_BULK, .sample_format=ORPHEUS_FORMAT_I32, .channels=1, .sample_rate=0, .block_size=1, .is_variable=false, .channels_param=NULL},
};

static const OrpheusParameter params[] = {
    {.id="initial_value", .name="首次反馈读值", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="write_after_call", .name="交付返回后提交反馈", .type=ORPHEUS_VALUE_BOOL, .default_value={.type=ORPHEUS_VALUE_BOOL, .value.b=false}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
};

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.scalar_memory", .version="1.1.0", .abi_version=7,
    .ports=ports, .port_count=6,
    .params=params, .param_count=2,
    .state_size=sizeof(orpheus_builtin_scalar_memory_State), .alignment=_Alignof(orpheus_builtin_scalar_memory_State),
    .scratch_size=0, .latency_samples=0,
    .realtime_safe=true, .supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

typedef struct orpheus_builtin_scalar_memory_Parameters {
    float initial_value;
    bool write_after_call;
} orpheus_builtin_scalar_memory_Parameters;
static void snapshot_parameters(const orpheus_builtin_scalar_memory_State* s, orpheus_builtin_scalar_memory_Parameters* p) {
    (void)s; memset(p, 0, sizeof(*p));
    p->initial_value=s->initial_value;
    p->write_after_call=s->write_after_call;
}
static void commit_parameters(orpheus_builtin_scalar_memory_State* s, const orpheus_builtin_scalar_memory_Parameters* p) {
    (void)s; (void)p;
    s->initial_value=p->initial_value;
    s->write_after_call=p->write_after_call;
}
static void default_parameters(orpheus_builtin_scalar_memory_Parameters* p) {
    memset(p, 0, sizeof(*p));
    p->initial_value=0.000000000e+00f;
    p->write_after_call=false;
}
static int update_parameter(orpheus_builtin_scalar_memory_Parameters* p, const char* key, const OrpheusValue* value) {
    if (!p || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "initial_value") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->initial_value=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "write_after_call") == 0) {
        if (value->type!=ORPHEUS_VALUE_BOOL) return ORPHEUS_ERR_INVALID_ARG;
        p->write_after_call=value->value.b;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int parameter_value(const orpheus_builtin_scalar_memory_Parameters* p, const char* key, OrpheusValue* value) {
    (void)p; (void)key; (void)value;
    if (strcmp(key, "initial_value")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->initial_value; return ORPHEUS_OK;
    }
    if (strcmp(key, "write_after_call")==0) {
        value->type=ORPHEUS_VALUE_BOOL; value->value.b=p->write_after_call; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int set_parameter(void* state, const char* key, const OrpheusValue* value) {
    orpheus_builtin_scalar_memory_State* s=(orpheus_builtin_scalar_memory_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_scalar_memory_Parameters before, candidate; snapshot_parameters(s, &before); candidate=before;
    int result=update_parameter(&candidate, key, value);
    if (result!=ORPHEUS_OK) return result;
    OrpheusValue checked; result=parameter_value(&candidate, key, &checked);
    if (result!=ORPHEUS_OK) return result;
    result=orpheus_builtin_scalar_memory_set_parameter(s, key, &checked);
    if (result!=ORPHEUS_OK) { commit_parameters(s, &before); return result; }
    orpheus_builtin_scalar_memory_Parameters after; snapshot_parameters(s, &after);
    result=update_parameter(&after, key, &checked);
    if (result!=ORPHEUS_OK) { commit_parameters(s, &before); return result; }
    candidate=after;
    s->_orpheus_cleanup_pending=true;
    commit_parameters(s, &candidate); return ORPHEUS_OK;
}
static int get_parameter(void* state, const char* key, OrpheusValue* value) {
    orpheus_builtin_scalar_memory_State* s=(orpheus_builtin_scalar_memory_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "initial_value")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->initial_value; return ORPHEUS_OK;
    }
    if (strcmp(key, "write_after_call")==0) {
        value->type=ORPHEUS_VALUE_BOOL; value->value.b=s->write_after_call; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int configure_parameters(orpheus_builtin_scalar_memory_Parameters* p, const OrpheusConfig* config) {
    if (!config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    default_parameters(p);
    for (uint32_t i=0; i<config->param_count; ++i) {
        int result=update_parameter(p, config->param_ids[i], &config->param_values[i]);
        if (result!=ORPHEUS_OK) return result;
    }
    return ORPHEUS_OK;
}
static int cleanup(orpheus_builtin_scalar_memory_State* s) {
    int result=ORPHEUS_OK;
    if (s->_orpheus_cleanup_pending) {
        s->_orpheus_cleanup_pending=false;
        memset(&s->algorithm, 0, sizeof(s->algorithm));
    }
    return result;
}
static int prepare(void* state, const OrpheusConfig* config) {
    orpheus_builtin_scalar_memory_State* s=(orpheus_builtin_scalar_memory_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_scalar_memory_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    result=cleanup(s); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate);
    s->_orpheus_cleanup_pending=true;
    result=orpheus_builtin_scalar_memory_prepare(s, config);
    if (result!=ORPHEUS_OK) { (void)cleanup(s); return result; }
    return ORPHEUS_OK;
}
static int create(void** state, const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=NULL;
    if (!config || !config->state_block) return ORPHEUS_ERR_INVALID_ARG;
    if ((uintptr_t)config->state_block % descriptor.alignment) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_scalar_memory_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    orpheus_builtin_scalar_memory_State* s=(orpheus_builtin_scalar_memory_State*)config->state_block;
    memset(s, 0, sizeof(*s)); commit_parameters(s, &candidate);
    s->_orpheus_alive=true; *state=s; return ORPHEUS_OK;
}
static int destroy(void* state) {
    orpheus_builtin_scalar_memory_State* s=(orpheus_builtin_scalar_memory_State*)state;
    if (!s) return ORPHEUS_ERR_INVALID_ARG;
    if (!s->_orpheus_alive) return ORPHEUS_OK;
    int result=cleanup(s); s->_orpheus_alive=false; return result;
}
static int reset(void* state) {
    orpheus_builtin_scalar_memory_State* s=(orpheus_builtin_scalar_memory_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_builtin_scalar_memory_reset(s);
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    orpheus_builtin_scalar_memory_State* s=(orpheus_builtin_scalar_memory_State*)state;
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_SLOT(reg, s, initial_value, ORPHEUS_SLOT_SETTING, "initial_value", "首次反馈读值", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, write_after_call, ORPHEUS_SLOT_SETTING, "write_after_call", "交付返回后提交反馈", ORPHEUS_VALUE_BOOL, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
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
    orpheus_builtin_scalar_memory_State* s=(orpheus_builtin_scalar_memory_State*)state;
    orpheus_builtin_scalar_memory_IO io={0};
    if (!s || !s->_orpheus_alive || !ctx) return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_payload_in=(double)(1);
    if (!isfinite(_orpheus_payload_in) || _orpheus_payload_in < 1
            || _orpheus_payload_in > UINT32_MAX || _orpheus_payload_in != (uint32_t)_orpheus_payload_in)
        return ORPHEUS_ERR_INVALID_ARG;
    io.in.input_index=0;
    io.in.buffer=(ctx->inputs && ctx->input_count > (0)) ? ctx->inputs[0] : NULL;
    if (!valid_buffer(io.in.buffer, (uint32_t)(1),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.in.samples=io.in.buffer ? (const float*)io.in.buffer->data : NULL;
    if (io.in.buffer && (io.in.buffer->frame_capacity < (uint32_t)_orpheus_payload_in
            || (io.in.buffer->frame_count != 0 && io.in.buffer->frame_count != (uint32_t)_orpheus_payload_in)))
        return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_payload_update=(double)(1);
    if (!isfinite(_orpheus_payload_update) || _orpheus_payload_update < 1
            || _orpheus_payload_update > UINT32_MAX || _orpheus_payload_update != (uint32_t)_orpheus_payload_update)
        return ORPHEUS_ERR_INVALID_ARG;
    io.update.input_index=1;
    io.update.buffer=(ctx->inputs && ctx->input_count > (1)) ? ctx->inputs[1] : NULL;
    if (!valid_buffer(io.update.buffer, (uint32_t)(1),
        ORPHEUS_FORMAT_I32, sizeof(int32_t), _Alignof(int32_t)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.update.samples=io.update.buffer ? (const int32_t*)io.update.buffer->data : NULL;
    if (io.update.buffer && io.update.buffer->frame_count != (uint32_t)_orpheus_payload_update)
        return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_payload_initial=(double)(1);
    if (!isfinite(_orpheus_payload_initial) || _orpheus_payload_initial < 1
            || _orpheus_payload_initial > UINT32_MAX || _orpheus_payload_initial != (uint32_t)_orpheus_payload_initial)
        return ORPHEUS_ERR_INVALID_ARG;
    io.initial.input_index=2;
    io.initial.buffer=(ctx->inputs && ctx->input_count > (2)) ? ctx->inputs[2] : NULL;
    if (!valid_buffer(io.initial.buffer, (uint32_t)(1),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.initial.samples=io.initial.buffer ? (const float*)io.initial.buffer->data : NULL;
    if (io.initial.buffer && io.initial.buffer->frame_count != (uint32_t)_orpheus_payload_initial)
        return ORPHEUS_ERR_INVALID_ARG;
    io.tick.input_index=3;
    if (ctx->inputs && ctx->input_count>(3) && ctx->inputs[3])
        return ORPHEUS_ERR_INVALID_ARG;
    io.tick.delivery=ctx->trigger_input==(3) ? ctx->delivery : NULL;
    if (io.tick.delivery && io.tick.delivery->buffer) return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_payload_out=(double)(1);
    if (!isfinite(_orpheus_payload_out) || _orpheus_payload_out < 1
            || _orpheus_payload_out > UINT32_MAX || _orpheus_payload_out != (uint32_t)_orpheus_payload_out)
        return ORPHEUS_ERR_INVALID_ARG;
    io.out.active=true;
    io.out.drive_port_no=1u;
    io.out.buffer=(ctx->outputs && ctx->output_count > (0)) ? ctx->outputs[0] : NULL;
    if (!valid_buffer(io.out.buffer, (uint32_t)(1),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.out.samples=io.out.buffer ? (float*)io.out.buffer->data : NULL;
    if (io.out.buffer) {
        if ((uint32_t)_orpheus_payload_out > io.out.buffer->frame_capacity) return ORPHEUS_ERR_INVALID_ARG;
        io.out.buffer->frame_count=(uint32_t)_orpheus_payload_out;
    }
    io.out.delivery=(OrpheusDelivery){.buffer=io.out.buffer, .frame_index=ctx->frame_index,
        .epoch=ctx->epoch, .timeline_flags=ctx->timeline_flags};
    double _orpheus_payload_enabled=(double)(1);
    if (!isfinite(_orpheus_payload_enabled) || _orpheus_payload_enabled < 1
            || _orpheus_payload_enabled > UINT32_MAX || _orpheus_payload_enabled != (uint32_t)_orpheus_payload_enabled)
        return ORPHEUS_ERR_INVALID_ARG;
    io.enabled.active=true;
    io.enabled.drive_port_no=0;
    io.enabled.buffer=(ctx->outputs && ctx->output_count > (1)) ? ctx->outputs[1] : NULL;
    if (!valid_buffer(io.enabled.buffer, (uint32_t)(1),
        ORPHEUS_FORMAT_I32, sizeof(int32_t), _Alignof(int32_t)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.enabled.samples=io.enabled.buffer ? (int32_t*)io.enabled.buffer->data : NULL;
    if (io.enabled.buffer) {
        if ((uint32_t)_orpheus_payload_enabled > io.enabled.buffer->frame_capacity) return ORPHEUS_ERR_INVALID_ARG;
        io.enabled.buffer->frame_count=(uint32_t)_orpheus_payload_enabled;
    }
    io.enabled.delivery=(OrpheusDelivery){.buffer=io.enabled.buffer, .frame_index=ctx->frame_index,
        .epoch=ctx->epoch, .timeline_flags=ctx->timeline_flags};
    return orpheus_builtin_scalar_memory_algorithm(s, &io, ctx);
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
