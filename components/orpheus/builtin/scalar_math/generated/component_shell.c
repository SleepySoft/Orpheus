#include "component_state.h"
#include <string.h>
#include <math.h>

static const OrpheusPort ports[] = {
    {.id="a", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_BULK, .sample_format=ORPHEUS_FORMAT_F32, .channels=1, .sample_rate=0, .block_size=1, .is_variable=false, .channels_param=NULL},
    {.id="b", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_BULK, .sample_format=ORPHEUS_FORMAT_F32, .channels=1, .sample_rate=0, .block_size=1, .is_variable=false, .channels_param=NULL},
    {.id="c", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_BULK, .sample_format=ORPHEUS_FORMAT_F32, .channels=1, .sample_rate=0, .block_size=1, .is_variable=false, .channels_param=NULL},
    {.id="tick", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_TRIGGER, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=false, .channels_param=NULL},
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_BULK, .sample_format=ORPHEUS_FORMAT_F32, .channels=1, .sample_rate=0, .block_size=1, .is_variable=false, .channels_param=NULL},
};

static const OrpheusParameter params[] = {
    {.id="operation", .name="运算(0最小/1最大/2大于/3不等/4选择/5直通/6加/7减/8乘/9除/10绝对值/11对数)", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=5}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_i32=0, .max_i32=11},
    {.id="a_default", .name="未连接a的值", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="b_default", .name="未连接b的值", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="c_default", .name="未连接c的值", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
};

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.scalar_math", .version="1.2.0", .abi_version=7,
    .ports=ports, .port_count=5,
    .params=params, .param_count=4,
    .state_size=sizeof(orpheus_builtin_scalar_math_State), .alignment=_Alignof(orpheus_builtin_scalar_math_State),
    .scratch_size=0, .latency_samples=0,
    .realtime_safe=true, .supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

typedef struct orpheus_builtin_scalar_math_Parameters {
    int32_t operation;
    float a_default;
    float b_default;
    float c_default;
} orpheus_builtin_scalar_math_Parameters;
static void snapshot_parameters(const orpheus_builtin_scalar_math_State* s, orpheus_builtin_scalar_math_Parameters* p) {
    (void)s; memset(p, 0, sizeof(*p));
    p->operation=s->operation;
    p->a_default=s->a_default;
    p->b_default=s->b_default;
    p->c_default=s->c_default;
}
static void commit_parameters(orpheus_builtin_scalar_math_State* s, const orpheus_builtin_scalar_math_Parameters* p) {
    (void)s; (void)p;
    s->operation=p->operation;
    s->a_default=p->a_default;
    s->b_default=p->b_default;
    s->c_default=p->c_default;
}
static void default_parameters(orpheus_builtin_scalar_math_Parameters* p) {
    memset(p, 0, sizeof(*p));
    p->operation=5;
    p->a_default=0.000000000e+00f;
    p->b_default=0.000000000e+00f;
    p->c_default=0.000000000e+00f;
}
static int update_parameter(orpheus_builtin_scalar_math_Parameters* p, const char* key, const OrpheusValue* value) {
    if (!p || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "operation") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<0 || value->value.i32>11) return ORPHEUS_ERR_INVALID_ARG;
        p->operation=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "a_default") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->a_default=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "b_default") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->b_default=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "c_default") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->c_default=value->value.f32;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int set_parameter(void* state, const char* key, const OrpheusValue* value) {
    orpheus_builtin_scalar_math_State* s=(orpheus_builtin_scalar_math_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_scalar_math_Parameters before, candidate; snapshot_parameters(s, &before); candidate=before;
    int result=update_parameter(&candidate, key, value);
    if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate); return ORPHEUS_OK;
}
static int get_parameter(void* state, const char* key, OrpheusValue* value) {
    orpheus_builtin_scalar_math_State* s=(orpheus_builtin_scalar_math_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "operation")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->operation; return ORPHEUS_OK;
    }
    if (strcmp(key, "a_default")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->a_default; return ORPHEUS_OK;
    }
    if (strcmp(key, "b_default")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->b_default; return ORPHEUS_OK;
    }
    if (strcmp(key, "c_default")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->c_default; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int configure_parameters(orpheus_builtin_scalar_math_Parameters* p, const OrpheusConfig* config) {
    if (!config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    default_parameters(p);
    for (uint32_t i=0; i<config->param_count; ++i) {
        int result=update_parameter(p, config->param_ids[i], &config->param_values[i]);
        if (result!=ORPHEUS_OK) return result;
    }
    return ORPHEUS_OK;
}
static int cleanup(orpheus_builtin_scalar_math_State* s) {
    int result=ORPHEUS_OK;
    if (s->_orpheus_cleanup_pending) {
        s->_orpheus_cleanup_pending=false;
        memset(&s->algorithm, 0, sizeof(s->algorithm));
    }
    return result;
}
static int prepare(void* state, const OrpheusConfig* config) {
    orpheus_builtin_scalar_math_State* s=(orpheus_builtin_scalar_math_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_scalar_math_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    result=cleanup(s); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate);
    s->_orpheus_cleanup_pending=true;
    return ORPHEUS_OK;
}
static int create(void** state, const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=NULL;
    if (!config || !config->state_block) return ORPHEUS_ERR_INVALID_ARG;
    if ((uintptr_t)config->state_block % descriptor.alignment) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_scalar_math_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    orpheus_builtin_scalar_math_State* s=(orpheus_builtin_scalar_math_State*)config->state_block;
    memset(s, 0, sizeof(*s)); commit_parameters(s, &candidate);
    s->_orpheus_alive=true; *state=s; return ORPHEUS_OK;
}
static int destroy(void* state) {
    orpheus_builtin_scalar_math_State* s=(orpheus_builtin_scalar_math_State*)state;
    if (!s) return ORPHEUS_ERR_INVALID_ARG;
    if (!s->_orpheus_alive) return ORPHEUS_OK;
    int result=cleanup(s); s->_orpheus_alive=false; return result;
}
static int reset(void* state) {
    orpheus_builtin_scalar_math_State* s=(orpheus_builtin_scalar_math_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    memset(&s->algorithm, 0, sizeof(s->algorithm)); return ORPHEUS_OK;
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    orpheus_builtin_scalar_math_State* s=(orpheus_builtin_scalar_math_State*)state;
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_SLOT(reg, s, operation, ORPHEUS_SLOT_SETTING, "operation", "运算(0最小/1最大/2大于/3不等/4选择/5直通/6加/7减/8乘/9除/10绝对值/11对数)", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_i32=0, .max_i32=11)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, a_default, ORPHEUS_SLOT_SETTING, "a_default", "未连接a的值", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, b_default, ORPHEUS_SLOT_SETTING, "b_default", "未连接b的值", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, c_default, ORPHEUS_SLOT_SETTING, "c_default", "未连接c的值", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
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
    orpheus_builtin_scalar_math_State* s=(orpheus_builtin_scalar_math_State*)state;
    orpheus_builtin_scalar_math_IO io={0};
    if (!s || !s->_orpheus_alive || !ctx) return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_payload_a=(double)(1);
    if (!isfinite(_orpheus_payload_a) || _orpheus_payload_a < 1
            || _orpheus_payload_a > UINT32_MAX || _orpheus_payload_a != (uint32_t)_orpheus_payload_a)
        return ORPHEUS_ERR_INVALID_ARG;
    io.a.input_index=0;
    io.a.buffer=(ctx->inputs && ctx->input_count > (0)) ? ctx->inputs[0] : NULL;
    if (!valid_buffer(io.a.buffer, (uint32_t)(1),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.a.samples=io.a.buffer ? (const float*)io.a.buffer->data : NULL;
    if (io.a.buffer && io.a.buffer->frame_count != (uint32_t)_orpheus_payload_a)
        return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_payload_b=(double)(1);
    if (!isfinite(_orpheus_payload_b) || _orpheus_payload_b < 1
            || _orpheus_payload_b > UINT32_MAX || _orpheus_payload_b != (uint32_t)_orpheus_payload_b)
        return ORPHEUS_ERR_INVALID_ARG;
    io.b.input_index=1;
    io.b.buffer=(ctx->inputs && ctx->input_count > (1)) ? ctx->inputs[1] : NULL;
    if (!valid_buffer(io.b.buffer, (uint32_t)(1),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.b.samples=io.b.buffer ? (const float*)io.b.buffer->data : NULL;
    if (io.b.buffer && io.b.buffer->frame_count != (uint32_t)_orpheus_payload_b)
        return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_payload_c=(double)(1);
    if (!isfinite(_orpheus_payload_c) || _orpheus_payload_c < 1
            || _orpheus_payload_c > UINT32_MAX || _orpheus_payload_c != (uint32_t)_orpheus_payload_c)
        return ORPHEUS_ERR_INVALID_ARG;
    io.c.input_index=2;
    io.c.buffer=(ctx->inputs && ctx->input_count > (2)) ? ctx->inputs[2] : NULL;
    if (!valid_buffer(io.c.buffer, (uint32_t)(1),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.c.samples=io.c.buffer ? (const float*)io.c.buffer->data : NULL;
    if (io.c.buffer && io.c.buffer->frame_count != (uint32_t)_orpheus_payload_c)
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
    return orpheus_builtin_scalar_math_algorithm(s, &io, ctx);
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
