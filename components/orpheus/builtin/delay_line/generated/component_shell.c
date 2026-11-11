#include "component_state.h"
#include <string.h>
#include <math.h>

#include <stdlib.h>
#include <errno.h>
#include <ctype.h>

_Static_assert(_Generic(&(((orpheus_builtin_delay_line_State*)0)->algorithm.delays_samples), float (*)[64]: 1, default: 0),
    "BULK delays_samples: storage 类型或固定容量不匹配");
_Static_assert(_Generic(&(((orpheus_builtin_delay_line_State*)0)->delays_i32), int32_t (*)[64]: 1, default: 0),
    "BULK delays_i32: storage 类型或固定容量不匹配");
static const OrpheusPort ports[] = {
    {.id="in", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
};

static const OrpheusParameter params[] = {
    {.id="channels", .name="通道数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=2}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=1, .max_i32=64},
    {.id="max_delay_samples", .name="最大延迟样本数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=4800}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_i32=1, .max_i32=192000},
    {.id="delays_samples", .name="每通道延迟样本数", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str=""}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=false, .persistent=true, .affects_signature=false},
    {.id="delay_samples", .name="公共延迟样本数（-1使用逐通道配置）", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=-1}, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_i32=-1, .max_i32=192000},
    {.id="delay_vector_mode", .name="逐通道配置类型（0浮点样本，1整数样本）", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=0}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_i32=0, .max_i32=1},
    {.id="initial_delays_i32", .name="初始整数延迟数组", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str=""}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="delays_i32", .name="每通道整数延迟样本数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str="0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0"}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_i32=0, .max_i32=192000},
};

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.delay_line", .version="1.3.0", .abi_version=7,
    .ports=ports, .port_count=2,
    .params=params, .param_count=7,
    .state_size=sizeof(orpheus_builtin_delay_line_State), .alignment=_Alignof(orpheus_builtin_delay_line_State),
    .scratch_size=0, .latency_samples=0,
    .realtime_safe=true, .supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

static void snapshot_parameters(const orpheus_builtin_delay_line_State* s, orpheus_builtin_delay_line_Parameters* p) {
    (void)s; memset(p, 0, sizeof(*p));
    p->channels=s->channels;
    p->max_delay_samples=s->max_delay_samples;
    memcpy(p->delays_samples, s->delays_samples, sizeof(p->delays_samples));
    p->delay_samples=s->delay_samples;
    p->delay_vector_mode=s->delay_vector_mode;
    memcpy(p->initial_delays_i32, s->initial_delays_i32, sizeof(p->initial_delays_i32));
    memcpy(p->delays_i32, s->delays_i32, sizeof(p->delays_i32));
}
static void commit_parameters(orpheus_builtin_delay_line_State* s, const orpheus_builtin_delay_line_Parameters* p) {
    (void)s; (void)p;
    s->channels=p->channels;
    s->max_delay_samples=p->max_delay_samples;
    memcpy(s->delays_samples, p->delays_samples, sizeof(s->delays_samples));
    s->delay_samples=p->delay_samples;
    s->delay_vector_mode=p->delay_vector_mode;
    memcpy(s->initial_delays_i32, p->initial_delays_i32, sizeof(s->initial_delays_i32));
    memcpy(s->delays_i32, p->delays_i32, sizeof(s->delays_i32));
}
static void default_parameters(orpheus_builtin_delay_line_Parameters* p) {
    memset(p, 0, sizeof(*p));
    p->channels=2;
    p->max_delay_samples=4800;
    memcpy(p->delays_samples, "", 1);
    p->delay_samples=-1;
    p->delay_vector_mode=0;
    memcpy(p->initial_delays_i32, "", 1);
    const int32_t initial_delays_i32[64]={0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    memcpy(p->delays_i32, initial_delays_i32, sizeof(p->delays_i32));
}
static int resource_count_delays_samples(const orpheus_builtin_delay_line_Parameters* p, uint32_t* count) {
    (void)p; uint32_t product=1;
    int64_t dimension0=p->channels;
    if (dimension0<1 || (uint64_t)dimension0>64u/product) return ORPHEUS_ERR_INVALID_ARG;
    product*=(uint32_t)dimension0;
    *count=product; return ORPHEUS_OK;
}
static int resource_count_delays_i32(const orpheus_builtin_delay_line_Parameters* p, uint32_t* count) {
    (void)p; uint32_t product=1;
    int64_t dimension0=p->channels;
    if (dimension0<1 || (uint64_t)dimension0>64u/product) return ORPHEUS_ERR_INVALID_ARG;
    product*=(uint32_t)dimension0;
    *count=product; return ORPHEUS_OK;
}
static int validate_resource_counts(const orpheus_builtin_delay_line_Parameters* p) {
    uint32_t count; int result;
    result=resource_count_delays_samples(p, &count);
    if (result!=ORPHEUS_OK) return result;
    result=resource_count_delays_i32(p, &count);
    if (result!=ORPHEUS_OK) return result;
    return ORPHEUS_OK;
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
static int update_parameter(orpheus_builtin_delay_line_Parameters* p, const char* key, const OrpheusValue* value) {
    if (!p || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "channels") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>64) return ORPHEUS_ERR_INVALID_ARG;
        p->channels=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "max_delay_samples") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>192000) return ORPHEUS_ERR_INVALID_ARG;
        p->max_delay_samples=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "delays_samples") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->delays_samples), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->delays_samples)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->delays_samples, text, sizeof(text));
        return ORPHEUS_OK;
    }
    if (strcmp(key, "delay_samples") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<-1 || value->value.i32>192000) return ORPHEUS_ERR_INVALID_ARG;
        p->delay_samples=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "delay_vector_mode") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<0 || value->value.i32>1) return ORPHEUS_ERR_INVALID_ARG;
        p->delay_vector_mode=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "initial_delays_i32") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->initial_delays_i32), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->initial_delays_i32)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->initial_delays_i32, text, sizeof(text));
        return ORPHEUS_OK;
    }
    if (strcmp(key, "delays_i32") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING || !value->value.str) return ORPHEUS_ERR_INVALID_ARG;
        const char* cursor=value->value.str;
        for (uint32_t i=0; i<64; ++i) {
            while (*cursor && isspace((unsigned char)*cursor)) ++cursor;
            const char* start=cursor;
            char* end; errno=0;
            long long number=strtoll(cursor, &end, 10);
            if (end==cursor || errno==ERANGE || number<INT32_MIN || number>INT32_MAX)
                return ORPHEUS_ERR_INVALID_ARG;
            int32_t element=(int32_t)number;
            cursor=end;
            if (element<0 || element>192000) return ORPHEUS_ERR_INVALID_ARG;
            (void)start;
            while (*cursor && isspace((unsigned char)*cursor)) ++cursor;
            if (i+1<64) { if (*cursor!=',') return ORPHEUS_ERR_INVALID_ARG; ++cursor; }
            else if (*cursor) return ORPHEUS_ERR_INVALID_ARG;
            p->delays_i32[i]=element;
        }
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int parameter_value(const orpheus_builtin_delay_line_Parameters* p, const char* key, OrpheusValue* value) {
    (void)p; (void)key; (void)value;
    if (strcmp(key, "channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->channels; return ORPHEUS_OK;
    }
    if (strcmp(key, "max_delay_samples")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->max_delay_samples; return ORPHEUS_OK;
    }
    if (strcmp(key, "delays_samples")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=p->delays_samples; return ORPHEUS_OK;
    }
    if (strcmp(key, "delay_samples")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->delay_samples; return ORPHEUS_OK;
    }
    if (strcmp(key, "delay_vector_mode")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->delay_vector_mode; return ORPHEUS_OK;
    }
    if (strcmp(key, "initial_delays_i32")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=p->initial_delays_i32; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int set_parameter(void* state, const char* key, const OrpheusValue* value) {
    orpheus_builtin_delay_line_State* s=(orpheus_builtin_delay_line_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "delays_i32")==0) return ORPHEUS_ERR_UNSUPPORTED;
    orpheus_builtin_delay_line_Parameters before, candidate; snapshot_parameters(s, &before); candidate=before;
    int result=update_parameter(&candidate, key, value);
    if (result!=ORPHEUS_OK) return result;
    result=validate_resource_counts(&candidate); if (result!=ORPHEUS_OK) return result;
    result=orpheus_builtin_delay_line_validate(&candidate); if (result!=ORPHEUS_OK) return result;
    OrpheusValue checked; result=parameter_value(&candidate, key, &checked);
    if (result!=ORPHEUS_OK) return result;
    result=orpheus_builtin_delay_line_set_parameter(s, key, &checked);
    if (result!=ORPHEUS_OK) { commit_parameters(s, &before); return result; }
    orpheus_builtin_delay_line_Parameters after; snapshot_parameters(s, &after);
    result=update_parameter(&after, key, &checked);
    if (result!=ORPHEUS_OK) { commit_parameters(s, &before); return result; }
    candidate=after;
    s->_orpheus_cleanup_pending=true;
    commit_parameters(s, &candidate); return ORPHEUS_OK;
}
static int get_parameter(void* state, const char* key, OrpheusValue* value) {
    orpheus_builtin_delay_line_State* s=(orpheus_builtin_delay_line_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->channels; return ORPHEUS_OK;
    }
    if (strcmp(key, "max_delay_samples")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->max_delay_samples; return ORPHEUS_OK;
    }
    if (strcmp(key, "delays_samples")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->delays_samples; return ORPHEUS_OK;
    }
    if (strcmp(key, "delay_samples")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->delay_samples; return ORPHEUS_OK;
    }
    if (strcmp(key, "delay_vector_mode")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->delay_vector_mode; return ORPHEUS_OK;
    }
    if (strcmp(key, "initial_delays_i32")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->initial_delays_i32; return ORPHEUS_OK;
    }
    if (strcmp(key, "delays_i32")==0) return ORPHEUS_ERR_UNSUPPORTED;
    return ORPHEUS_ERR_NOT_FOUND;
}
static int configure_parameters(orpheus_builtin_delay_line_Parameters* p, const OrpheusConfig* config) {
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
    int result=validate_resource_counts(p); if (result!=ORPHEUS_OK) return result;
    return orpheus_builtin_delay_line_validate(p);
}
static int cleanup(orpheus_builtin_delay_line_State* s) {
    int result=ORPHEUS_OK;
    if (s->_orpheus_cleanup_pending) {
        s->_orpheus_cleanup_pending=false;
        result=orpheus_builtin_delay_line_destroy(s);
        memset(&s->algorithm, 0, sizeof(s->algorithm));
    }
    return result;
}
static int prepare(void* state, const OrpheusConfig* config) {
    orpheus_builtin_delay_line_State* s=(orpheus_builtin_delay_line_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_delay_line_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    result=cleanup(s); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate);
    s->_orpheus_cleanup_pending=true;
    result=orpheus_builtin_delay_line_prepare(s, config);
    if (result!=ORPHEUS_OK) { (void)cleanup(s); return result; }
    return ORPHEUS_OK;
}
static int create(void** state, const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=NULL;
    if (!config || !config->state_block) return ORPHEUS_ERR_INVALID_ARG;
    if ((uintptr_t)config->state_block % descriptor.alignment) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_delay_line_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    orpheus_builtin_delay_line_State* s=(orpheus_builtin_delay_line_State*)config->state_block;
    memset(s, 0, sizeof(*s)); commit_parameters(s, &candidate);
    s->_orpheus_alive=true; *state=s; return ORPHEUS_OK;
}
static int destroy(void* state) {
    orpheus_builtin_delay_line_State* s=(orpheus_builtin_delay_line_State*)state;
    if (!s) return ORPHEUS_ERR_INVALID_ARG;
    if (!s->_orpheus_alive) return ORPHEUS_OK;
    int result=cleanup(s); s->_orpheus_alive=false; return result;
}
static int reset(void* state) {
    orpheus_builtin_delay_line_State* s=(orpheus_builtin_delay_line_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_builtin_delay_line_reset(s);
}
int orpheus_builtin_delay_line_register_delays_samples(orpheus_builtin_delay_line_State* s, const OrpheusRegistry* reg) {
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_delay_line_Parameters current; snapshot_parameters(s, &current);
    uint32_t count_delays_samples;
    if (resource_count_delays_samples(&current, &count_delays_samples)!=ORPHEUS_OK) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_ARRAY(reg, s, algorithm.delays_samples, count_delays_samples, ORPHEUS_SLOT_BULK, "delays_samples", "每通道延迟样本数", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_DOUBLE_BUFFERED)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    return ORPHEUS_OK;
}
int orpheus_builtin_delay_line_register_delays_i32(orpheus_builtin_delay_line_State* s, const OrpheusRegistry* reg) {
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_delay_line_Parameters current; snapshot_parameters(s, &current);
    uint32_t count_delays_i32;
    if (resource_count_delays_i32(&current, &count_delays_i32)!=ORPHEUS_OK) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_ARRAY(reg, s, delays_i32, count_delays_i32, ORPHEUS_SLOT_BULK, "delays_i32", "每通道整数延迟样本数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_DOUBLE_BUFFERED, .min_i32=0, .max_i32=192000)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    return ORPHEUS_OK;
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    orpheus_builtin_delay_line_State* s=(orpheus_builtin_delay_line_State*)state;
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=1, .max_i32=64)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, max_delay_samples, ORPHEUS_SLOT_SETTING, "max_delay_samples", "最大延迟样本数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_i32=1, .max_i32=192000)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, delay_samples, ORPHEUS_SLOT_SETTING, "delay_samples", "公共延迟样本数（-1使用逐通道配置）", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_i32=-1, .max_i32=192000)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, delay_vector_mode, ORPHEUS_SLOT_SETTING, "delay_vector_mode", "逐通道配置类型（0浮点样本，1整数样本）", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_i32=0, .max_i32=1)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, initial_delays_i32, ORPHEUS_SLOT_SETTING, "initial_delays_i32", "初始整数延迟数组", ORPHEUS_VALUE_STRING, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    return orpheus_builtin_delay_line_register_slots(s, reg);
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
    orpheus_builtin_delay_line_State* s=(orpheus_builtin_delay_line_State*)state;
    orpheus_builtin_delay_line_IO io={0};
    if (!s || !s->_orpheus_alive || !ctx) return ORPHEUS_ERR_INVALID_ARG;
    io.in.input_index=0;
    io.in.buffer=(ctx->inputs && ctx->input_count > (0)) ? ctx->inputs[0] : NULL;
    if (!valid_buffer(io.in.buffer, (uint32_t)(s->channels),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.in.samples=io.in.buffer ? (const float*)io.in.buffer->data : NULL;
    io.out.active=true;
    io.out.drive_port_no=1u;
    io.out.buffer=(ctx->outputs && ctx->output_count > (0)) ? ctx->outputs[0] : NULL;
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
    return orpheus_builtin_delay_line_algorithm(s, &io, ctx);
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
