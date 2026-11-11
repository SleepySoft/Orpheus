#include "component_state.h"
#include <string.h>
#include <math.h>

_Static_assert(_Generic(&(((orpheus_builtin_gain_ramper_State*)0)->algorithm.current_gains), float (*)[32]: 1, default: 0),
    "BULK current_gains: storage 类型或固定容量不匹配");
_Static_assert(_Generic(&(((orpheus_builtin_gain_ramper_State*)0)->algorithm.target_gains), float (*)[32]: 1, default: 0),
    "BULK target_gains: storage 类型或固定容量不匹配");
_Static_assert(_Generic(&(((orpheus_builtin_gain_ramper_State*)0)->algorithm.coefficients), float (*)[32]: 1, default: 0),
    "BULK coefficients: storage 类型或固定容量不匹配");
_Static_assert(_Generic(&(((orpheus_builtin_gain_ramper_State*)0)->algorithm.remaining_blocks), int32_t (*)[32]: 1, default: 0),
    "BULK remaining_blocks: storage 类型或固定容量不匹配");
static const OrpheusPort ports[] = {
    {.id="in", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
    {.id="gain", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_CONTROL, .sample_format=ORPHEUS_FORMAT_F32, .channels=1, .sample_rate=0, .block_size=1, .is_variable=true, .channels_param=NULL},
};

static const OrpheusParameter params[] = {
    {.id="gain_db", .name="目标增益", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="dB", .readback=true, .persistent=true, .affects_signature=false, .min_f32=-9.600000000e+01f, .max_f32=2.400000000e+01f},
    {.id="ramp_ms", .name="斜坡时间", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=3.000000000e+01f}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="ms", .readback=true, .persistent=true, .affects_signature=false, .min_f32=0.000000000e+00f, .max_f32=5.000000000e+03f},
    {.id="channels", .name="通道数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=2}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=1, .max_i32=64},
    {.id="num_rampers", .name="ramper数量", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=1}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=1, .max_i32=32},
    {.id="chan_map", .name="通道映射", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str=""}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="execution_mode", .name="执行模式（0块级请求，1逐样本递推，2帧内幂次）", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=0}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_i32=0, .max_i32=2},
    {.id="current_gains", .name="各组当前线性增益", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str=""}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="target_gains", .name="各组目标线性增益", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str=""}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="coefficients", .name="各组逐样本乘数", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str=""}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="remaining_blocks", .name="各组剩余块数", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str=""}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="freeze", .name="冻结显式包络", .type=ORPHEUS_VALUE_BOOL, .default_value={.type=ORPHEUS_VALUE_BOOL, .value.b=false}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="gain_output", .name="输出块末通道增益", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=0}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=0, .max_i32=1},
    {.id="gain_channel", .name="观测通道（从0起）", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=0}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_i32=0, .max_i32=63},
};

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.gain_ramper", .version="1.4.0", .abi_version=7,
    .ports=ports, .port_count=3,
    .params=params, .param_count=13,
    .state_size=sizeof(orpheus_builtin_gain_ramper_State), .alignment=_Alignof(orpheus_builtin_gain_ramper_State),
    .scratch_size=0, .latency_samples=0,
    .realtime_safe=true, .supports_inplace=true
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

static void snapshot_parameters(const orpheus_builtin_gain_ramper_State* s, orpheus_builtin_gain_ramper_Parameters* p) {
    (void)s; memset(p, 0, sizeof(*p));
    p->gain_db=s->gain_db;
    p->ramp_ms=s->ramp_ms;
    p->channels=s->channels;
    p->num_rampers=s->num_rampers;
    memcpy(p->chan_map, s->chan_map, sizeof(p->chan_map));
    p->execution_mode=s->execution_mode;
    memcpy(p->current_gains, s->current_gains, sizeof(p->current_gains));
    memcpy(p->target_gains, s->target_gains, sizeof(p->target_gains));
    memcpy(p->coefficients, s->coefficients, sizeof(p->coefficients));
    memcpy(p->remaining_blocks, s->remaining_blocks, sizeof(p->remaining_blocks));
    p->freeze=s->freeze;
    p->gain_output=s->gain_output;
    p->gain_channel=s->gain_channel;
}
static void commit_parameters(orpheus_builtin_gain_ramper_State* s, const orpheus_builtin_gain_ramper_Parameters* p) {
    (void)s; (void)p;
    s->gain_db=p->gain_db;
    s->ramp_ms=p->ramp_ms;
    s->channels=p->channels;
    s->num_rampers=p->num_rampers;
    memcpy(s->chan_map, p->chan_map, sizeof(s->chan_map));
    s->execution_mode=p->execution_mode;
    memcpy(s->current_gains, p->current_gains, sizeof(s->current_gains));
    memcpy(s->target_gains, p->target_gains, sizeof(s->target_gains));
    memcpy(s->coefficients, p->coefficients, sizeof(s->coefficients));
    memcpy(s->remaining_blocks, p->remaining_blocks, sizeof(s->remaining_blocks));
    s->freeze=p->freeze;
    s->gain_output=p->gain_output;
    s->gain_channel=p->gain_channel;
}
static void default_parameters(orpheus_builtin_gain_ramper_Parameters* p) {
    memset(p, 0, sizeof(*p));
    p->gain_db=0.000000000e+00f;
    p->ramp_ms=3.000000000e+01f;
    p->channels=2;
    p->num_rampers=1;
    memcpy(p->chan_map, "", 1);
    p->execution_mode=0;
    memcpy(p->current_gains, "", 1);
    memcpy(p->target_gains, "", 1);
    memcpy(p->coefficients, "", 1);
    memcpy(p->remaining_blocks, "", 1);
    p->freeze=false;
    p->gain_output=0;
    p->gain_channel=0;
}
static int resource_count_current_gains(const orpheus_builtin_gain_ramper_Parameters* p, uint32_t* count) {
    (void)p; uint32_t product=1;
    int64_t dimension0=p->num_rampers;
    if (dimension0<1 || (uint64_t)dimension0>32u/product) return ORPHEUS_ERR_INVALID_ARG;
    product*=(uint32_t)dimension0;
    *count=product; return ORPHEUS_OK;
}
static int resource_count_target_gains(const orpheus_builtin_gain_ramper_Parameters* p, uint32_t* count) {
    (void)p; uint32_t product=1;
    int64_t dimension0=p->num_rampers;
    if (dimension0<1 || (uint64_t)dimension0>32u/product) return ORPHEUS_ERR_INVALID_ARG;
    product*=(uint32_t)dimension0;
    *count=product; return ORPHEUS_OK;
}
static int resource_count_coefficients(const orpheus_builtin_gain_ramper_Parameters* p, uint32_t* count) {
    (void)p; uint32_t product=1;
    int64_t dimension0=p->num_rampers;
    if (dimension0<1 || (uint64_t)dimension0>32u/product) return ORPHEUS_ERR_INVALID_ARG;
    product*=(uint32_t)dimension0;
    *count=product; return ORPHEUS_OK;
}
static int resource_count_remaining_blocks(const orpheus_builtin_gain_ramper_Parameters* p, uint32_t* count) {
    (void)p; uint32_t product=1;
    int64_t dimension0=p->num_rampers;
    if (dimension0<1 || (uint64_t)dimension0>32u/product) return ORPHEUS_ERR_INVALID_ARG;
    product*=(uint32_t)dimension0;
    *count=product; return ORPHEUS_OK;
}
static int validate_resource_counts(const orpheus_builtin_gain_ramper_Parameters* p) {
    uint32_t count; int result;
    result=resource_count_current_gains(p, &count);
    if (result!=ORPHEUS_OK) return result;
    result=resource_count_target_gains(p, &count);
    if (result!=ORPHEUS_OK) return result;
    result=resource_count_coefficients(p, &count);
    if (result!=ORPHEUS_OK) return result;
    result=resource_count_remaining_blocks(p, &count);
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
static int update_parameter(orpheus_builtin_gain_ramper_Parameters* p, const char* key, const OrpheusValue* value) {
    if (!p || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "gain_db") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<-9.600000000e+01f || value->value.f32>2.400000000e+01f) return ORPHEUS_ERR_INVALID_ARG;
        p->gain_db=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp_ms") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<0.000000000e+00f || value->value.f32>5.000000000e+03f) return ORPHEUS_ERR_INVALID_ARG;
        p->ramp_ms=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "channels") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>64) return ORPHEUS_ERR_INVALID_ARG;
        p->channels=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "num_rampers") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>32) return ORPHEUS_ERR_INVALID_ARG;
        p->num_rampers=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "chan_map") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->chan_map), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->chan_map)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->chan_map, text, sizeof(text));
        return ORPHEUS_OK;
    }
    if (strcmp(key, "execution_mode") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<0 || value->value.i32>2) return ORPHEUS_ERR_INVALID_ARG;
        p->execution_mode=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "current_gains") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->current_gains), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->current_gains)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->current_gains, text, sizeof(text));
        return ORPHEUS_OK;
    }
    if (strcmp(key, "target_gains") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->target_gains), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->target_gains)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->target_gains, text, sizeof(text));
        return ORPHEUS_OK;
    }
    if (strcmp(key, "coefficients") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->coefficients), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->coefficients)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->coefficients, text, sizeof(text));
        return ORPHEUS_OK;
    }
    if (strcmp(key, "remaining_blocks") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->remaining_blocks), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->remaining_blocks)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->remaining_blocks, text, sizeof(text));
        return ORPHEUS_OK;
    }
    if (strcmp(key, "freeze") == 0) {
        if (value->type!=ORPHEUS_VALUE_BOOL) return ORPHEUS_ERR_INVALID_ARG;
        p->freeze=value->value.b;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "gain_output") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<0 || value->value.i32>1) return ORPHEUS_ERR_INVALID_ARG;
        p->gain_output=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "gain_channel") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<0 || value->value.i32>63) return ORPHEUS_ERR_INVALID_ARG;
        p->gain_channel=value->value.i32;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int parameter_value(const orpheus_builtin_gain_ramper_Parameters* p, const char* key, OrpheusValue* value) {
    (void)p; (void)key; (void)value;
    if (strcmp(key, "gain_db")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->gain_db; return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp_ms")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->ramp_ms; return ORPHEUS_OK;
    }
    if (strcmp(key, "channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->channels; return ORPHEUS_OK;
    }
    if (strcmp(key, "num_rampers")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->num_rampers; return ORPHEUS_OK;
    }
    if (strcmp(key, "chan_map")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=p->chan_map; return ORPHEUS_OK;
    }
    if (strcmp(key, "execution_mode")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->execution_mode; return ORPHEUS_OK;
    }
    if (strcmp(key, "current_gains")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=p->current_gains; return ORPHEUS_OK;
    }
    if (strcmp(key, "target_gains")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=p->target_gains; return ORPHEUS_OK;
    }
    if (strcmp(key, "coefficients")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=p->coefficients; return ORPHEUS_OK;
    }
    if (strcmp(key, "remaining_blocks")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=p->remaining_blocks; return ORPHEUS_OK;
    }
    if (strcmp(key, "freeze")==0) {
        value->type=ORPHEUS_VALUE_BOOL; value->value.b=p->freeze; return ORPHEUS_OK;
    }
    if (strcmp(key, "gain_output")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->gain_output; return ORPHEUS_OK;
    }
    if (strcmp(key, "gain_channel")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->gain_channel; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int set_parameter(void* state, const char* key, const OrpheusValue* value) {
    orpheus_builtin_gain_ramper_State* s=(orpheus_builtin_gain_ramper_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_gain_ramper_Parameters before, candidate; snapshot_parameters(s, &before); candidate=before;
    int result=update_parameter(&candidate, key, value);
    if (result!=ORPHEUS_OK) return result;
    result=validate_resource_counts(&candidate); if (result!=ORPHEUS_OK) return result;
    result=orpheus_builtin_gain_ramper_validate(&candidate); if (result!=ORPHEUS_OK) return result;
    OrpheusValue checked; result=parameter_value(&candidate, key, &checked);
    if (result!=ORPHEUS_OK) return result;
    result=orpheus_builtin_gain_ramper_set_parameter(s, key, &checked);
    if (result!=ORPHEUS_OK) { commit_parameters(s, &before); return result; }
    orpheus_builtin_gain_ramper_Parameters after; snapshot_parameters(s, &after);
    result=update_parameter(&after, key, &checked);
    if (result!=ORPHEUS_OK) { commit_parameters(s, &before); return result; }
    candidate=after;
    s->_orpheus_cleanup_pending=true;
    commit_parameters(s, &candidate); return ORPHEUS_OK;
}
static int get_parameter(void* state, const char* key, OrpheusValue* value) {
    orpheus_builtin_gain_ramper_State* s=(orpheus_builtin_gain_ramper_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "gain_db")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->gain_db; return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp_ms")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->ramp_ms; return ORPHEUS_OK;
    }
    if (strcmp(key, "channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->channels; return ORPHEUS_OK;
    }
    if (strcmp(key, "num_rampers")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->num_rampers; return ORPHEUS_OK;
    }
    if (strcmp(key, "chan_map")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->chan_map; return ORPHEUS_OK;
    }
    if (strcmp(key, "execution_mode")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->execution_mode; return ORPHEUS_OK;
    }
    if (strcmp(key, "current_gains")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->current_gains; return ORPHEUS_OK;
    }
    if (strcmp(key, "target_gains")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->target_gains; return ORPHEUS_OK;
    }
    if (strcmp(key, "coefficients")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->coefficients; return ORPHEUS_OK;
    }
    if (strcmp(key, "remaining_blocks")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->remaining_blocks; return ORPHEUS_OK;
    }
    if (strcmp(key, "freeze")==0) {
        value->type=ORPHEUS_VALUE_BOOL; value->value.b=s->freeze; return ORPHEUS_OK;
    }
    if (strcmp(key, "gain_output")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->gain_output; return ORPHEUS_OK;
    }
    if (strcmp(key, "gain_channel")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->gain_channel; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int configure_parameters(orpheus_builtin_gain_ramper_Parameters* p, const OrpheusConfig* config) {
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
    return orpheus_builtin_gain_ramper_validate(p);
}
static int cleanup(orpheus_builtin_gain_ramper_State* s) {
    int result=ORPHEUS_OK;
    if (s->_orpheus_cleanup_pending) {
        s->_orpheus_cleanup_pending=false;
        memset(&s->algorithm, 0, sizeof(s->algorithm));
    }
    return result;
}
static int prepare(void* state, const OrpheusConfig* config) {
    orpheus_builtin_gain_ramper_State* s=(orpheus_builtin_gain_ramper_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_gain_ramper_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    result=cleanup(s); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate);
    s->_orpheus_cleanup_pending=true;
    result=orpheus_builtin_gain_ramper_prepare(s, config);
    if (result!=ORPHEUS_OK) { (void)cleanup(s); return result; }
    return ORPHEUS_OK;
}
static int create(void** state, const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=NULL;
    if (!config || !config->state_block) return ORPHEUS_ERR_INVALID_ARG;
    if ((uintptr_t)config->state_block % descriptor.alignment) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_gain_ramper_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    orpheus_builtin_gain_ramper_State* s=(orpheus_builtin_gain_ramper_State*)config->state_block;
    memset(s, 0, sizeof(*s)); commit_parameters(s, &candidate);
    s->_orpheus_alive=true; *state=s; return ORPHEUS_OK;
}
static int destroy(void* state) {
    orpheus_builtin_gain_ramper_State* s=(orpheus_builtin_gain_ramper_State*)state;
    if (!s) return ORPHEUS_ERR_INVALID_ARG;
    if (!s->_orpheus_alive) return ORPHEUS_OK;
    int result=cleanup(s); s->_orpheus_alive=false; return result;
}
static int reset(void* state) {
    orpheus_builtin_gain_ramper_State* s=(orpheus_builtin_gain_ramper_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_builtin_gain_ramper_reset(s);
}
int orpheus_builtin_gain_ramper_register_current_gains(orpheus_builtin_gain_ramper_State* s, const OrpheusRegistry* reg) {
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_gain_ramper_Parameters current; snapshot_parameters(s, &current);
    uint32_t count_current_gains;
    if (resource_count_current_gains(&current, &count_current_gains)!=ORPHEUS_OK) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_ARRAY(reg, s, algorithm.current_gains, count_current_gains, ORPHEUS_SLOT_BULK, "current_gains", "各组当前线性增益", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_DOUBLE_BUFFERED)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    return ORPHEUS_OK;
}
int orpheus_builtin_gain_ramper_register_target_gains(orpheus_builtin_gain_ramper_State* s, const OrpheusRegistry* reg) {
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_gain_ramper_Parameters current; snapshot_parameters(s, &current);
    uint32_t count_target_gains;
    if (resource_count_target_gains(&current, &count_target_gains)!=ORPHEUS_OK) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_ARRAY(reg, s, algorithm.target_gains, count_target_gains, ORPHEUS_SLOT_BULK, "target_gains", "各组目标线性增益", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_DOUBLE_BUFFERED)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    return ORPHEUS_OK;
}
int orpheus_builtin_gain_ramper_register_coefficients(orpheus_builtin_gain_ramper_State* s, const OrpheusRegistry* reg) {
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_gain_ramper_Parameters current; snapshot_parameters(s, &current);
    uint32_t count_coefficients;
    if (resource_count_coefficients(&current, &count_coefficients)!=ORPHEUS_OK) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_ARRAY(reg, s, algorithm.coefficients, count_coefficients, ORPHEUS_SLOT_BULK, "coefficients", "各组逐样本乘数", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_DOUBLE_BUFFERED)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    return ORPHEUS_OK;
}
int orpheus_builtin_gain_ramper_register_remaining_blocks(orpheus_builtin_gain_ramper_State* s, const OrpheusRegistry* reg) {
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_gain_ramper_Parameters current; snapshot_parameters(s, &current);
    uint32_t count_remaining_blocks;
    if (resource_count_remaining_blocks(&current, &count_remaining_blocks)!=ORPHEUS_OK) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_ARRAY(reg, s, algorithm.remaining_blocks, count_remaining_blocks, ORPHEUS_SLOT_BULK, "remaining_blocks", "各组剩余块数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_DOUBLE_BUFFERED)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    return ORPHEUS_OK;
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    orpheus_builtin_gain_ramper_State* s=(orpheus_builtin_gain_ramper_State*)state;
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_SLOT(reg, s, gain_db, ORPHEUS_SLOT_SETTING, "gain_db", "目标增益", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="dB", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=-9.600000000e+01f, .max_f32=2.400000000e+01f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, ramp_ms, ORPHEUS_SLOT_SETTING, "ramp_ms", "斜坡时间", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="ms", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=0.000000000e+00f, .max_f32=5.000000000e+03f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=1, .max_i32=64)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, num_rampers, ORPHEUS_SLOT_SETTING, "num_rampers", "ramper数量", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=1, .max_i32=32)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, chan_map, ORPHEUS_SLOT_SETTING, "chan_map", "通道映射", ORPHEUS_VALUE_STRING, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, execution_mode, ORPHEUS_SLOT_SETTING, "execution_mode", "执行模式（0块级请求，1逐样本递推，2帧内幂次）", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_i32=0, .max_i32=2)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    int result_current_gains=orpheus_builtin_gain_ramper_register_current_gains(s, reg);
    if (result_current_gains!=ORPHEUS_OK) return result_current_gains;
    int result_target_gains=orpheus_builtin_gain_ramper_register_target_gains(s, reg);
    if (result_target_gains!=ORPHEUS_OK) return result_target_gains;
    int result_coefficients=orpheus_builtin_gain_ramper_register_coefficients(s, reg);
    if (result_coefficients!=ORPHEUS_OK) return result_coefficients;
    int result_remaining_blocks=orpheus_builtin_gain_ramper_register_remaining_blocks(s, reg);
    if (result_remaining_blocks!=ORPHEUS_OK) return result_remaining_blocks;
    if (ORPHEUS_REG_SLOT(reg, s, freeze, ORPHEUS_SLOT_SETTING, "freeze", "冻结显式包络", ORPHEUS_VALUE_BOOL, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, gain_output, ORPHEUS_SLOT_SETTING, "gain_output", "输出块末通道增益", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=0, .max_i32=1)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, gain_channel, ORPHEUS_SLOT_SETTING, "gain_channel", "观测通道（从0起）", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_i32=0, .max_i32=63)==ORPHEUS_SLOT_ID_INVALID)
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
    orpheus_builtin_gain_ramper_State* s=(orpheus_builtin_gain_ramper_State*)state;
    orpheus_builtin_gain_ramper_IO io={0};
    if (!s || !s->_orpheus_alive || !ctx) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t _orpheus_input_count=0, _orpheus_output_count=0;
    _orpheus_input_count+=1;
    _orpheus_output_count+=1;
    int64_t _orpheus_count_gain=s->gain_output;
    if (_orpheus_count_gain < 0 || _orpheus_count_gain > 1)
        return ORPHEUS_ERR_INVALID_ARG;
    io.gain_count=(uint32_t)_orpheus_count_gain;
    _orpheus_output_count+=io.gain_count;
    if (ctx->input_count!=_orpheus_input_count || ctx->output_count!=_orpheus_output_count)
        return ORPHEUS_ERR_INVALID_ARG;
    if (ctx->trigger_input!=UINT32_MAX && ctx->trigger_input>=_orpheus_input_count)
        return ORPHEUS_ERR_INVALID_ARG;
    if (ctx->delivery && ctx->trigger_input==UINT32_MAX) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t _orpheus_input_slot=0;
    io.in.input_index=_orpheus_input_slot;
    io.in.buffer=(ctx->inputs && ctx->input_count > (_orpheus_input_slot)) ? ctx->inputs[_orpheus_input_slot] : NULL;
    if (!valid_buffer(io.in.buffer, (uint32_t)(s->channels),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.in.samples=io.in.buffer ? (const float*)io.in.buffer->data : NULL;
    _orpheus_input_slot+=1;
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
    double _orpheus_payload_gain=(double)(1);
    if (!isfinite(_orpheus_payload_gain) || _orpheus_payload_gain < 1
            || _orpheus_payload_gain > UINT32_MAX || _orpheus_payload_gain != (uint32_t)_orpheus_payload_gain)
        return ORPHEUS_ERR_INVALID_ARG;
    for (uint32_t _orpheus_i=0; _orpheus_i<io.gain_count; ++_orpheus_i) {
    io.gain[_orpheus_i].active=true;
    io.gain[_orpheus_i].drive_port_no=2u+_orpheus_i;
    io.gain[_orpheus_i].buffer=(ctx->outputs && ctx->output_count > (_orpheus_output_slot+_orpheus_i)) ? ctx->outputs[_orpheus_output_slot+_orpheus_i] : NULL;
    if (!valid_buffer(io.gain[_orpheus_i].buffer, (uint32_t)(1),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.gain[_orpheus_i].samples=io.gain[_orpheus_i].buffer ? (float*)io.gain[_orpheus_i].buffer->data : NULL;
    if (io.gain[_orpheus_i].buffer) {
        if ((uint32_t)_orpheus_payload_gain > io.gain[_orpheus_i].buffer->frame_capacity) return ORPHEUS_ERR_INVALID_ARG;
        io.gain[_orpheus_i].buffer->frame_count=(uint32_t)_orpheus_payload_gain;
    }
    io.gain[_orpheus_i].delivery=(OrpheusDelivery){.buffer=io.gain[_orpheus_i].buffer, .frame_index=ctx->frame_index,
        .epoch=ctx->epoch, .timeline_flags=ctx->timeline_flags};
    }
    _orpheus_output_slot+=io.gain_count;
    return orpheus_builtin_gain_ramper_algorithm(s, &io, ctx);
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
