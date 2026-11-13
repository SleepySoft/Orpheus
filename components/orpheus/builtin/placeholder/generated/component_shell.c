#include "component_state.h"
#include <string.h>
#include <math.h>

static const OrpheusParameter params[] = {
    {.id="interface", .name="占位接口", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str="[{\"id\":\"in\",\"direction\":\"input\",\"channels\":2},{\"id\":\"out\",\"direction\":\"output\",\"channels\":2}]"}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true},
    {.id="mode", .name="桩行为", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str="passthrough"}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="routes", .name="输出路由（按输出顺序填输入序号，逗号分隔；-1 置零）", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str="0"}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
};

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.placeholder", .version="1.1.0", .abi_version=7,
    .ports=NULL, .port_count=0,
    .params=params, .param_count=3,
    .state_size=sizeof(orpheus_builtin_placeholder_State), .alignment=_Alignof(orpheus_builtin_placeholder_State),
    .scratch_size=0, .latency_samples=0,
    .realtime_safe=true, .supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

typedef struct orpheus_builtin_placeholder_Parameters {
    char interface[4096];
    char mode[256];
    char routes[256];
} orpheus_builtin_placeholder_Parameters;
static void snapshot_parameters(const orpheus_builtin_placeholder_State* s, orpheus_builtin_placeholder_Parameters* p) {
    (void)s; memset(p, 0, sizeof(*p));
    memcpy(p->interface, s->interface, sizeof(p->interface));
    memcpy(p->mode, s->mode, sizeof(p->mode));
    memcpy(p->routes, s->routes, sizeof(p->routes));
}
static void commit_parameters(orpheus_builtin_placeholder_State* s, const orpheus_builtin_placeholder_Parameters* p) {
    (void)s; (void)p;
    memcpy(s->interface, p->interface, sizeof(s->interface));
    memcpy(s->mode, p->mode, sizeof(s->mode));
    memcpy(s->routes, p->routes, sizeof(s->routes));
}
static void default_parameters(orpheus_builtin_placeholder_Parameters* p) {
    memset(p, 0, sizeof(*p));
    memcpy(p->interface, "[{\"id\":\"in\",\"direction\":\"input\",\"channels\":2},{\"id\":\"out\",\"direction\":\"output\",\"channels\":2}]", 94);
    memcpy(p->mode, "passthrough", 12);
    memcpy(p->routes, "0", 2);
}
static int valid_utf8_string(const char* text, size_t capacity, size_t* length) {
    if (!text) return ORPHEUS_ERR_INVALID_ARG;
    size_t i=0;
    while (i<capacity) {
        unsigned char c=(unsigned char)text[i];
        if (!c) { *length=i; return ORPHEUS_OK; }
        if (c<0x80) { ++i; continue; }
        size_t n=(c>=0xC2 && c<=0xDF) ? 2 : (c>=0xE0 && c<=0xEF) ? 3 :
                 (c>=0xF0 && c<=0xF4) ? 4 : 0;
        if (!n || n>capacity-i) return ORPHEUS_ERR_INVALID_ARG;
        for (size_t j=1; j<n; ++j)
            if (((unsigned char)text[i+j] & 0xC0)!=0x80) return ORPHEUS_ERR_INVALID_ARG;
        unsigned char second=(unsigned char)text[i+1];
        if ((c==0xE0 && second<0xA0) || (c==0xED && second>=0xA0) ||
            (c==0xF0 && second<0x90) || (c==0xF4 && second>=0x90))
            return ORPHEUS_ERR_INVALID_ARG;
        i+=n;
    }
    return ORPHEUS_ERR_INVALID_ARG;
}
static int update_parameter(orpheus_builtin_placeholder_Parameters* p, const char* key, const OrpheusValue* value) {
    if (!p || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "interface") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->interface), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->interface)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->interface, text, sizeof(text));
        return ORPHEUS_OK;
    }
    if (strcmp(key, "mode") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->mode), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->mode)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->mode, text, sizeof(text));
        return ORPHEUS_OK;
    }
    if (strcmp(key, "routes") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->routes), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->routes)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->routes, text, sizeof(text));
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int parameter_value(const orpheus_builtin_placeholder_Parameters* p, const char* key, OrpheusValue* value) {
    (void)p; (void)key; (void)value;
    if (strcmp(key, "interface")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=p->interface; return ORPHEUS_OK;
    }
    if (strcmp(key, "mode")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=p->mode; return ORPHEUS_OK;
    }
    if (strcmp(key, "routes")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=p->routes; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int set_parameter(void* state, const char* key, const OrpheusValue* value) {
    orpheus_builtin_placeholder_State* s=(orpheus_builtin_placeholder_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_placeholder_Parameters before, candidate; snapshot_parameters(s, &before); candidate=before;
    int result=update_parameter(&candidate, key, value);
    if (result!=ORPHEUS_OK) return result;
    OrpheusValue checked; result=parameter_value(&candidate, key, &checked);
    if (result!=ORPHEUS_OK) return result;
    result=orpheus_builtin_placeholder_set_parameter(s, key, &checked);
    if (result!=ORPHEUS_OK) { commit_parameters(s, &before); return result; }
    orpheus_builtin_placeholder_Parameters after; snapshot_parameters(s, &after);
    result=update_parameter(&after, key, &checked);
    if (result!=ORPHEUS_OK) { commit_parameters(s, &before); return result; }
    candidate=after;
    s->_orpheus_cleanup_pending=true;
    commit_parameters(s, &candidate); return ORPHEUS_OK;
}
static int get_parameter(void* state, const char* key, OrpheusValue* value) {
    orpheus_builtin_placeholder_State* s=(orpheus_builtin_placeholder_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "interface")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->interface; return ORPHEUS_OK;
    }
    if (strcmp(key, "mode")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->mode; return ORPHEUS_OK;
    }
    if (strcmp(key, "routes")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->routes; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int configure_parameters(orpheus_builtin_placeholder_Parameters* p, const OrpheusConfig* config) {
    if (!config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    default_parameters(p);
    for (uint32_t i=0; i<config->param_count; ++i) {
        int result=update_parameter(p, config->param_ids[i], &config->param_values[i]);
        if (result!=ORPHEUS_OK) return result;
    }
    return ORPHEUS_OK;
}
static int cleanup(orpheus_builtin_placeholder_State* s) {
    int result=ORPHEUS_OK;
    if (s->_orpheus_cleanup_pending) {
        s->_orpheus_cleanup_pending=false;
        memset(&s->algorithm, 0, sizeof(s->algorithm));
    }
    return result;
}
static int prepare(void* state, const OrpheusConfig* config) {
    orpheus_builtin_placeholder_State* s=(orpheus_builtin_placeholder_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_placeholder_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    result=cleanup(s); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate);
    s->_orpheus_cleanup_pending=true;
    result=orpheus_builtin_placeholder_prepare(s, config);
    if (result!=ORPHEUS_OK) { (void)cleanup(s); return result; }
    return ORPHEUS_OK;
}
static int create(void** state, const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=NULL;
    if (!config || !config->state_block) return ORPHEUS_ERR_INVALID_ARG;
    if ((uintptr_t)config->state_block % descriptor.alignment) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_placeholder_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    orpheus_builtin_placeholder_State* s=(orpheus_builtin_placeholder_State*)config->state_block;
    memset(s, 0, sizeof(*s)); commit_parameters(s, &candidate);
    s->_orpheus_alive=true; *state=s; return ORPHEUS_OK;
}
static int destroy(void* state) {
    orpheus_builtin_placeholder_State* s=(orpheus_builtin_placeholder_State*)state;
    if (!s) return ORPHEUS_ERR_INVALID_ARG;
    if (!s->_orpheus_alive) return ORPHEUS_OK;
    int result=cleanup(s); s->_orpheus_alive=false; return result;
}
static int reset(void* state) {
    orpheus_builtin_placeholder_State* s=(orpheus_builtin_placeholder_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_builtin_placeholder_reset(s);
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    orpheus_builtin_placeholder_State* s=(orpheus_builtin_placeholder_State*)state;
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_SLOT(reg, s, interface, ORPHEUS_SLOT_SETTING, "interface", "占位接口", ORPHEUS_VALUE_STRING, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, mode, ORPHEUS_SLOT_SETTING, "mode", "桩行为", ORPHEUS_VALUE_STRING, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, routes, ORPHEUS_SLOT_SETTING, "routes", "输出路由（按输出顺序填输入序号，逗号分隔；-1 置零）", ORPHEUS_VALUE_STRING, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    return ORPHEUS_OK;
}
static int process(void* state, const OrpheusProcessContext* ctx) {
    orpheus_builtin_placeholder_State* s=(orpheus_builtin_placeholder_State*)state;
    orpheus_builtin_placeholder_IO io={0};
    if (!s || !s->_orpheus_alive || !ctx) return ORPHEUS_ERR_INVALID_ARG;
    io.inputs=ctx->inputs; io.input_count=ctx->input_count;
    io.outputs=ctx->outputs; io.output_count=ctx->output_count;
    return orpheus_builtin_placeholder_algorithm(s, &io, ctx);
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
