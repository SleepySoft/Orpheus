#include "component_state.h"
#include <string.h>
#include <math.h>

static const OrpheusPort ports[] = {
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_TRIGGER, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=false, .channels_param=NULL},
};

static const OrpheusParameter params[] = {
    {.id="frequency_hz", .name="驱动频率", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=100}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="Hz", .readback=true, .persistent=true, .affects_signature=false, .min_i32=1, .max_i32=192000},
};

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.task_source", .version="1.1.0", .abi_version=7,
    .ports=ports, .port_count=1,
    .params=params, .param_count=1,
    .state_size=sizeof(orpheus_builtin_task_source_State), .alignment=_Alignof(orpheus_builtin_task_source_State),
    .scratch_size=0, .latency_samples=0,
    .realtime_safe=true, .supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

typedef struct orpheus_builtin_task_source_Parameters {
    int32_t frequency_hz;
} orpheus_builtin_task_source_Parameters;
static void snapshot_parameters(const orpheus_builtin_task_source_State* s, orpheus_builtin_task_source_Parameters* p) {
    (void)s; memset(p, 0, sizeof(*p));
    p->frequency_hz=s->frequency_hz;
}
static void commit_parameters(orpheus_builtin_task_source_State* s, const orpheus_builtin_task_source_Parameters* p) {
    (void)s; (void)p;
    s->frequency_hz=p->frequency_hz;
}
static void default_parameters(orpheus_builtin_task_source_Parameters* p) {
    memset(p, 0, sizeof(*p));
    p->frequency_hz=100;
}
static int update_parameter(orpheus_builtin_task_source_Parameters* p, const char* key, const OrpheusValue* value) {
    if (!p || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "frequency_hz") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>192000) return ORPHEUS_ERR_INVALID_ARG;
        p->frequency_hz=value->value.i32;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int parameter_value(const orpheus_builtin_task_source_Parameters* p, const char* key, OrpheusValue* value) {
    (void)p; (void)key; (void)value;
    if (strcmp(key, "frequency_hz")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->frequency_hz; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int set_parameter(void* state, const char* key, const OrpheusValue* value) {
    orpheus_builtin_task_source_State* s=(orpheus_builtin_task_source_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_task_source_Parameters before, candidate; snapshot_parameters(s, &before); candidate=before;
    int result=update_parameter(&candidate, key, value);
    if (result!=ORPHEUS_OK) return result;
    OrpheusValue checked; result=parameter_value(&candidate, key, &checked);
    if (result!=ORPHEUS_OK) return result;
    result=orpheus_builtin_task_source_set_parameter(s, key, &checked);
    if (result!=ORPHEUS_OK) { commit_parameters(s, &before); return result; }
    orpheus_builtin_task_source_Parameters after; snapshot_parameters(s, &after);
    result=update_parameter(&after, key, &checked);
    if (result!=ORPHEUS_OK) { commit_parameters(s, &before); return result; }
    candidate=after;
    s->_orpheus_cleanup_pending=true;
    commit_parameters(s, &candidate); return ORPHEUS_OK;
}
static int get_parameter(void* state, const char* key, OrpheusValue* value) {
    orpheus_builtin_task_source_State* s=(orpheus_builtin_task_source_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "frequency_hz")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->frequency_hz; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int configure_parameters(orpheus_builtin_task_source_Parameters* p, const OrpheusConfig* config) {
    if (!config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    default_parameters(p);
    for (uint32_t i=0; i<config->param_count; ++i) {
        int result=update_parameter(p, config->param_ids[i], &config->param_values[i]);
        if (result!=ORPHEUS_OK) return result;
    }
    return ORPHEUS_OK;
}
static int cleanup(orpheus_builtin_task_source_State* s) {
    int result=ORPHEUS_OK;
    if (s->_orpheus_cleanup_pending) {
        s->_orpheus_cleanup_pending=false;
        memset(&s->algorithm, 0, sizeof(s->algorithm));
    }
    return result;
}
static int prepare(void* state, const OrpheusConfig* config) {
    orpheus_builtin_task_source_State* s=(orpheus_builtin_task_source_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_task_source_Parameters candidate; int result=configure_parameters(&candidate, config);
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
    orpheus_builtin_task_source_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    orpheus_builtin_task_source_State* s=(orpheus_builtin_task_source_State*)config->state_block;
    memset(s, 0, sizeof(*s)); commit_parameters(s, &candidate);
    s->_orpheus_alive=true; *state=s; return ORPHEUS_OK;
}
static int destroy(void* state) {
    orpheus_builtin_task_source_State* s=(orpheus_builtin_task_source_State*)state;
    if (!s) return ORPHEUS_ERR_INVALID_ARG;
    if (!s->_orpheus_alive) return ORPHEUS_OK;
    int result=cleanup(s); s->_orpheus_alive=false; return result;
}
static int reset(void* state) {
    orpheus_builtin_task_source_State* s=(orpheus_builtin_task_source_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    memset(&s->algorithm, 0, sizeof(s->algorithm)); return ORPHEUS_OK;
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    orpheus_builtin_task_source_State* s=(orpheus_builtin_task_source_State*)state;
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_SLOT(reg, s, frequency_hz, ORPHEUS_SLOT_SETTING, "frequency_hz", "驱动频率", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="Hz", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_i32=1, .max_i32=192000)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    return ORPHEUS_OK;
}
static int process(void* state, const OrpheusProcessContext* ctx) {
    orpheus_builtin_task_source_State* s=(orpheus_builtin_task_source_State*)state;
    orpheus_builtin_task_source_IO io={0};
    if (!s || !s->_orpheus_alive || !ctx) return ORPHEUS_ERR_INVALID_ARG;
    io.out.active=true;
    io.out.drive_port_no=1u;
    if (ctx->outputs && ctx->output_count>(0) && ctx->outputs[0])
        return ORPHEUS_ERR_INVALID_ARG;
    io.out.delivery=(OrpheusDelivery){.buffer=NULL,.frame_index=ctx->frame_index,
        .epoch=ctx->epoch,.timeline_flags=ctx->timeline_flags};
    return orpheus_builtin_task_source_algorithm(s, &io, ctx);
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
