#include "component_state.h"
#include <string.h>
#include <math.h>

static const OrpheusPort ports[] = {
    {.id="in", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_BULK, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
    {.id="bound", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_BULK, .sample_format=ORPHEUS_FORMAT_F32, .channels=1, .sample_rate=0, .block_size=1, .is_variable=false, .channels_param=NULL},
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_BULK, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
};

static const OrpheusParameter params[] = {
    {.id="channels", .name="通道数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=1}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=1, .max_i32=128},
    {.id="elements", .name="每通道元素数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=1}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=1, .max_i32=65536},
    {.id="attack", .name="每次正向最大变化", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=1.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_f32=0.000000000e+00f, .max_f32=3.402823466e+38f},
    {.id="decay", .name="每次负向最小变化", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=-1.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_f32=-3.402823466e+38f, .max_f32=0.000000000e+00f},
    {.id="threshold", .name="方向保持阈值", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_f32=0.000000000e+00f, .max_f32=3.402823466e+38f},
    {.id="slow", .name="边界慢速率幅值", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_f32=0.000000000e+00f, .max_f32=3.402823466e+38f},
    {.id="initial", .name="初始输出", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
};

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.bulk_guarded_slew", .version="1.1.0", .abi_version=7,
    .ports=ports, .port_count=3,
    .params=params, .param_count=7,
    .state_size=sizeof(orpheus_builtin_bulk_guarded_slew_State), .alignment=_Alignof(orpheus_builtin_bulk_guarded_slew_State),
    .scratch_size=0, .latency_samples=0,
    .realtime_safe=true, .supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

typedef struct orpheus_builtin_bulk_guarded_slew_Parameters {
    int32_t channels;
    int32_t elements;
    float attack;
    float decay;
    float threshold;
    float slow;
    float initial;
} orpheus_builtin_bulk_guarded_slew_Parameters;
static void snapshot_parameters(const orpheus_builtin_bulk_guarded_slew_State* s, orpheus_builtin_bulk_guarded_slew_Parameters* p) {
    (void)s; memset(p, 0, sizeof(*p));
    p->channels=s->channels;
    p->elements=s->elements;
    p->attack=s->attack;
    p->decay=s->decay;
    p->threshold=s->threshold;
    p->slow=s->slow;
    p->initial=s->initial;
}
static void commit_parameters(orpheus_builtin_bulk_guarded_slew_State* s, const orpheus_builtin_bulk_guarded_slew_Parameters* p) {
    (void)s; (void)p;
    s->channels=p->channels;
    s->elements=p->elements;
    s->attack=p->attack;
    s->decay=p->decay;
    s->threshold=p->threshold;
    s->slow=p->slow;
    s->initial=p->initial;
}
static void default_parameters(orpheus_builtin_bulk_guarded_slew_Parameters* p) {
    memset(p, 0, sizeof(*p));
    p->channels=1;
    p->elements=1;
    p->attack=1.000000000e+00f;
    p->decay=-1.000000000e+00f;
    p->threshold=0.000000000e+00f;
    p->slow=0.000000000e+00f;
    p->initial=0.000000000e+00f;
}
static int update_parameter(orpheus_builtin_bulk_guarded_slew_Parameters* p, const char* key, const OrpheusValue* value) {
    if (!p || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "channels") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>128) return ORPHEUS_ERR_INVALID_ARG;
        p->channels=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "elements") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>65536) return ORPHEUS_ERR_INVALID_ARG;
        p->elements=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "attack") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<0.000000000e+00f || value->value.f32>3.402823466e+38f) return ORPHEUS_ERR_INVALID_ARG;
        p->attack=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "decay") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<-3.402823466e+38f || value->value.f32>0.000000000e+00f) return ORPHEUS_ERR_INVALID_ARG;
        p->decay=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "threshold") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<0.000000000e+00f || value->value.f32>3.402823466e+38f) return ORPHEUS_ERR_INVALID_ARG;
        p->threshold=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "slow") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<0.000000000e+00f || value->value.f32>3.402823466e+38f) return ORPHEUS_ERR_INVALID_ARG;
        p->slow=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "initial") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->initial=value->value.f32;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int set_parameter(void* state, const char* key, const OrpheusValue* value) {
    orpheus_builtin_bulk_guarded_slew_State* s=(orpheus_builtin_bulk_guarded_slew_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_bulk_guarded_slew_Parameters before, candidate; snapshot_parameters(s, &before); candidate=before;
    int result=update_parameter(&candidate, key, value);
    if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate); return ORPHEUS_OK;
}
static int get_parameter(void* state, const char* key, OrpheusValue* value) {
    orpheus_builtin_bulk_guarded_slew_State* s=(orpheus_builtin_bulk_guarded_slew_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->channels; return ORPHEUS_OK;
    }
    if (strcmp(key, "elements")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->elements; return ORPHEUS_OK;
    }
    if (strcmp(key, "attack")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->attack; return ORPHEUS_OK;
    }
    if (strcmp(key, "decay")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->decay; return ORPHEUS_OK;
    }
    if (strcmp(key, "threshold")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->threshold; return ORPHEUS_OK;
    }
    if (strcmp(key, "slow")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->slow; return ORPHEUS_OK;
    }
    if (strcmp(key, "initial")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->initial; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int configure_parameters(orpheus_builtin_bulk_guarded_slew_Parameters* p, const OrpheusConfig* config) {
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
    return ORPHEUS_OK;
}
static int cleanup(orpheus_builtin_bulk_guarded_slew_State* s) {
    int result=ORPHEUS_OK;
    if (s->_orpheus_cleanup_pending) {
        s->_orpheus_cleanup_pending=false;
        result=orpheus_builtin_bulk_guarded_slew_destroy(s);
        memset(&s->algorithm, 0, sizeof(s->algorithm));
    }
    return result;
}
static int prepare(void* state, const OrpheusConfig* config) {
    orpheus_builtin_bulk_guarded_slew_State* s=(orpheus_builtin_bulk_guarded_slew_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_bulk_guarded_slew_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    result=cleanup(s); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate);
    s->_orpheus_cleanup_pending=true;
    result=orpheus_builtin_bulk_guarded_slew_prepare(s, config);
    if (result!=ORPHEUS_OK) { (void)cleanup(s); return result; }
    return ORPHEUS_OK;
}
static int create(void** state, const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=NULL;
    if (!config || !config->state_block) return ORPHEUS_ERR_INVALID_ARG;
    if ((uintptr_t)config->state_block % descriptor.alignment) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_bulk_guarded_slew_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    orpheus_builtin_bulk_guarded_slew_State* s=(orpheus_builtin_bulk_guarded_slew_State*)config->state_block;
    memset(s, 0, sizeof(*s)); commit_parameters(s, &candidate);
    s->_orpheus_alive=true; *state=s; return ORPHEUS_OK;
}
static int destroy(void* state) {
    orpheus_builtin_bulk_guarded_slew_State* s=(orpheus_builtin_bulk_guarded_slew_State*)state;
    if (!s) return ORPHEUS_ERR_INVALID_ARG;
    if (!s->_orpheus_alive) return ORPHEUS_OK;
    int result=cleanup(s); s->_orpheus_alive=false; return result;
}
static int reset(void* state) {
    orpheus_builtin_bulk_guarded_slew_State* s=(orpheus_builtin_bulk_guarded_slew_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_builtin_bulk_guarded_slew_reset(s);
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    orpheus_builtin_bulk_guarded_slew_State* s=(orpheus_builtin_bulk_guarded_slew_State*)state;
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=1, .max_i32=128)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, elements, ORPHEUS_SLOT_SETTING, "elements", "每通道元素数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=1, .max_i32=65536)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, attack, ORPHEUS_SLOT_SETTING, "attack", "每次正向最大变化", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=0.000000000e+00f, .max_f32=3.402823466e+38f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, decay, ORPHEUS_SLOT_SETTING, "decay", "每次负向最小变化", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=-3.402823466e+38f, .max_f32=0.000000000e+00f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, threshold, ORPHEUS_SLOT_SETTING, "threshold", "方向保持阈值", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=0.000000000e+00f, .max_f32=3.402823466e+38f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, slow, ORPHEUS_SLOT_SETTING, "slow", "边界慢速率幅值", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=0.000000000e+00f, .max_f32=3.402823466e+38f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, initial, ORPHEUS_SLOT_SETTING, "initial", "初始输出", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
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
    orpheus_builtin_bulk_guarded_slew_State* s=(orpheus_builtin_bulk_guarded_slew_State*)state;
    orpheus_builtin_bulk_guarded_slew_IO io={0};
    if (!s || !s->_orpheus_alive || !ctx) return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_payload_in=(double)(s->elements);
    if (!isfinite(_orpheus_payload_in) || _orpheus_payload_in < 1
            || _orpheus_payload_in > UINT32_MAX || _orpheus_payload_in != (uint32_t)_orpheus_payload_in)
        return ORPHEUS_ERR_INVALID_ARG;
    io.in.input_index=0;
    io.in.buffer=(ctx->inputs && ctx->input_count > (0)) ? ctx->inputs[0] : NULL;
    if (!valid_buffer(io.in.buffer, (uint32_t)(s->channels),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.in.samples=io.in.buffer ? (const float*)io.in.buffer->data : NULL;
    if (io.in.buffer && io.in.buffer->frame_count != (uint32_t)_orpheus_payload_in)
        return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_payload_bound=(double)(1);
    if (!isfinite(_orpheus_payload_bound) || _orpheus_payload_bound < 1
            || _orpheus_payload_bound > UINT32_MAX || _orpheus_payload_bound != (uint32_t)_orpheus_payload_bound)
        return ORPHEUS_ERR_INVALID_ARG;
    io.bound.input_index=1;
    io.bound.buffer=(ctx->inputs && ctx->input_count > (1)) ? ctx->inputs[1] : NULL;
    if (!valid_buffer(io.bound.buffer, (uint32_t)(1),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.bound.samples=io.bound.buffer ? (const float*)io.bound.buffer->data : NULL;
    if (io.bound.buffer && io.bound.buffer->frame_count != (uint32_t)_orpheus_payload_bound)
        return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_payload_out=(double)(s->elements);
    if (!isfinite(_orpheus_payload_out) || _orpheus_payload_out < 1
            || _orpheus_payload_out > UINT32_MAX || _orpheus_payload_out != (uint32_t)_orpheus_payload_out)
        return ORPHEUS_ERR_INVALID_ARG;
    io.out.active=true;
    io.out.drive_port_no=1u;
    io.out.buffer=(ctx->outputs && ctx->output_count > (0)) ? ctx->outputs[0] : NULL;
    if (!valid_buffer(io.out.buffer, (uint32_t)(s->channels),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.out.samples=io.out.buffer ? (float*)io.out.buffer->data : NULL;
    if (io.out.buffer) {
        if ((uint32_t)_orpheus_payload_out > io.out.buffer->frame_capacity) return ORPHEUS_ERR_INVALID_ARG;
        io.out.buffer->frame_count=(uint32_t)_orpheus_payload_out;
    }
    io.out.delivery=(OrpheusDelivery){.buffer=io.out.buffer, .frame_index=ctx->frame_index,
        .epoch=ctx->epoch, .timeline_flags=ctx->timeline_flags};
    return orpheus_builtin_bulk_guarded_slew_algorithm(s, &io, ctx);
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
