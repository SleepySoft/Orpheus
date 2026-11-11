#include "component_state.h"
#include <string.h>
#include <math.h>

_Static_assert(_Generic(&(((orpheus_builtin_fade_State*)0)->algorithm.current_gains), float (*)[4]: 1, default: 0),
    "BULK current_gains: storage 类型或固定容量不匹配");
_Static_assert(_Generic(&(((orpheus_builtin_fade_State*)0)->algorithm.target_gains), float (*)[4]: 1, default: 0),
    "BULK target_gains: storage 类型或固定容量不匹配");
_Static_assert(_Generic(&(((orpheus_builtin_fade_State*)0)->algorithm.coefficients), float (*)[4]: 1, default: 0),
    "BULK coefficients: storage 类型或固定容量不匹配");
_Static_assert(_Generic(&(((orpheus_builtin_fade_State*)0)->algorithm.remaining_blocks), int32_t (*)[4]: 1, default: 0),
    "BULK remaining_blocks: storage 类型或固定容量不匹配");
static const OrpheusPort ports[] = {
    {.id="in", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
    {.id="lpf", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
};

static const OrpheusParameter params[] = {
    {.id="fade", .name="衰减", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_f32=-1.000000000e+00f, .max_f32=1.000000000e+00f},
    {.id="crossover_hz", .name="分频点", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=4.000000000e+02f}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="Hz", .readback=true, .persistent=true, .affects_signature=false, .min_f32=1.000000000e+02f, .max_f32=2.000000000e+03f},
    {.id="channels", .name="通道数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=4}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=2, .max_i32=32},
    {.id="front_channels", .name="前组通道数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=2}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_i32=1, .max_i32=31},
    {.id="ramp_ms", .name="斜坡时间", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=5.000000000e+01f}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="ms", .readback=true, .persistent=true, .affects_signature=false, .min_f32=0.000000000e+00f, .max_f32=1.000000000e+03f},
    {.id="execution_mode", .name="执行模式（0内部分频，1外部分频显式包络）", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=0}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=0, .max_i32=1},
    {.id="chan_map", .name="通道到前后组映射（-1正零）", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str=""}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="current_gains", .name="高频前后/低频前后当前增益", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str=""}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="target_gains", .name="高频前后/低频前后目标增益", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str=""}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="coefficients", .name="高频前后/低频前后幂次乘数", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str=""}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="remaining_blocks", .name="四组剩余块数（仅前两组驱动）", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str=""}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="freeze", .name="冻结显式包络", .type=ORPHEUS_VALUE_BOOL, .default_value={.type=ORPHEUS_VALUE_BOOL, .value.b=false}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
};

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.fade", .version="1.1.0", .abi_version=7,
    .ports=ports, .port_count=3,
    .params=params, .param_count=12,
    .state_size=sizeof(orpheus_builtin_fade_State), .alignment=_Alignof(orpheus_builtin_fade_State),
    .scratch_size=0, .latency_samples=0,
    .realtime_safe=true, .supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

static void snapshot_parameters(const orpheus_builtin_fade_State* s, orpheus_builtin_fade_Parameters* p) {
    (void)s; memset(p, 0, sizeof(*p));
    p->fade=s->fade;
    p->crossover_hz=s->crossover_hz;
    p->channels=s->channels;
    p->front_channels=s->front_channels;
    p->ramp_ms=s->ramp_ms;
    p->execution_mode=s->execution_mode;
    memcpy(p->chan_map, s->chan_map, sizeof(p->chan_map));
    memcpy(p->current_gains, s->current_gains, sizeof(p->current_gains));
    memcpy(p->target_gains, s->target_gains, sizeof(p->target_gains));
    memcpy(p->coefficients, s->coefficients, sizeof(p->coefficients));
    memcpy(p->remaining_blocks, s->remaining_blocks, sizeof(p->remaining_blocks));
    p->freeze=s->freeze;
}
static void commit_parameters(orpheus_builtin_fade_State* s, const orpheus_builtin_fade_Parameters* p) {
    (void)s; (void)p;
    s->fade=p->fade;
    s->crossover_hz=p->crossover_hz;
    s->channels=p->channels;
    s->front_channels=p->front_channels;
    s->ramp_ms=p->ramp_ms;
    s->execution_mode=p->execution_mode;
    memcpy(s->chan_map, p->chan_map, sizeof(s->chan_map));
    memcpy(s->current_gains, p->current_gains, sizeof(s->current_gains));
    memcpy(s->target_gains, p->target_gains, sizeof(s->target_gains));
    memcpy(s->coefficients, p->coefficients, sizeof(s->coefficients));
    memcpy(s->remaining_blocks, p->remaining_blocks, sizeof(s->remaining_blocks));
    s->freeze=p->freeze;
}
static void default_parameters(orpheus_builtin_fade_Parameters* p) {
    memset(p, 0, sizeof(*p));
    p->fade=0.000000000e+00f;
    p->crossover_hz=4.000000000e+02f;
    p->channels=4;
    p->front_channels=2;
    p->ramp_ms=5.000000000e+01f;
    p->execution_mode=0;
    memcpy(p->chan_map, "", 1);
    memcpy(p->current_gains, "", 1);
    memcpy(p->target_gains, "", 1);
    memcpy(p->coefficients, "", 1);
    memcpy(p->remaining_blocks, "", 1);
    p->freeze=false;
}
static int resource_count_current_gains(const orpheus_builtin_fade_Parameters* p, uint32_t* count) {
    (void)p; uint32_t product=1;
    int64_t dimension0=4;
    if (dimension0<1 || (uint64_t)dimension0>4u/product) return ORPHEUS_ERR_INVALID_ARG;
    product*=(uint32_t)dimension0;
    *count=product; return ORPHEUS_OK;
}
static int resource_count_target_gains(const orpheus_builtin_fade_Parameters* p, uint32_t* count) {
    (void)p; uint32_t product=1;
    int64_t dimension0=4;
    if (dimension0<1 || (uint64_t)dimension0>4u/product) return ORPHEUS_ERR_INVALID_ARG;
    product*=(uint32_t)dimension0;
    *count=product; return ORPHEUS_OK;
}
static int resource_count_coefficients(const orpheus_builtin_fade_Parameters* p, uint32_t* count) {
    (void)p; uint32_t product=1;
    int64_t dimension0=4;
    if (dimension0<1 || (uint64_t)dimension0>4u/product) return ORPHEUS_ERR_INVALID_ARG;
    product*=(uint32_t)dimension0;
    *count=product; return ORPHEUS_OK;
}
static int resource_count_remaining_blocks(const orpheus_builtin_fade_Parameters* p, uint32_t* count) {
    (void)p; uint32_t product=1;
    int64_t dimension0=4;
    if (dimension0<1 || (uint64_t)dimension0>4u/product) return ORPHEUS_ERR_INVALID_ARG;
    product*=(uint32_t)dimension0;
    *count=product; return ORPHEUS_OK;
}
static int validate_resource_counts(const orpheus_builtin_fade_Parameters* p) {
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
static int update_parameter(orpheus_builtin_fade_Parameters* p, const char* key, const OrpheusValue* value) {
    if (!p || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "fade") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<-1.000000000e+00f || value->value.f32>1.000000000e+00f) return ORPHEUS_ERR_INVALID_ARG;
        p->fade=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "crossover_hz") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<1.000000000e+02f || value->value.f32>2.000000000e+03f) return ORPHEUS_ERR_INVALID_ARG;
        p->crossover_hz=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "channels") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<2 || value->value.i32>32) return ORPHEUS_ERR_INVALID_ARG;
        p->channels=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "front_channels") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>31) return ORPHEUS_ERR_INVALID_ARG;
        p->front_channels=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp_ms") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<0.000000000e+00f || value->value.f32>1.000000000e+03f) return ORPHEUS_ERR_INVALID_ARG;
        p->ramp_ms=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "execution_mode") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<0 || value->value.i32>1) return ORPHEUS_ERR_INVALID_ARG;
        p->execution_mode=value->value.i32;
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
    return ORPHEUS_ERR_NOT_FOUND;
}
static int parameter_value(const orpheus_builtin_fade_Parameters* p, const char* key, OrpheusValue* value) {
    (void)p; (void)key; (void)value;
    if (strcmp(key, "fade")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->fade; return ORPHEUS_OK;
    }
    if (strcmp(key, "crossover_hz")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->crossover_hz; return ORPHEUS_OK;
    }
    if (strcmp(key, "channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->channels; return ORPHEUS_OK;
    }
    if (strcmp(key, "front_channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->front_channels; return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp_ms")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->ramp_ms; return ORPHEUS_OK;
    }
    if (strcmp(key, "execution_mode")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->execution_mode; return ORPHEUS_OK;
    }
    if (strcmp(key, "chan_map")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=p->chan_map; return ORPHEUS_OK;
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
    return ORPHEUS_ERR_NOT_FOUND;
}
static int set_parameter(void* state, const char* key, const OrpheusValue* value) {
    orpheus_builtin_fade_State* s=(orpheus_builtin_fade_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_fade_Parameters before, candidate; snapshot_parameters(s, &before); candidate=before;
    int result=update_parameter(&candidate, key, value);
    if (result!=ORPHEUS_OK) return result;
    result=validate_resource_counts(&candidate); if (result!=ORPHEUS_OK) return result;
    result=orpheus_builtin_fade_validate(&candidate); if (result!=ORPHEUS_OK) return result;
    OrpheusValue checked; result=parameter_value(&candidate, key, &checked);
    if (result!=ORPHEUS_OK) return result;
    result=orpheus_builtin_fade_set_parameter(s, key, &checked);
    if (result!=ORPHEUS_OK) { commit_parameters(s, &before); return result; }
    orpheus_builtin_fade_Parameters after; snapshot_parameters(s, &after);
    result=update_parameter(&after, key, &checked);
    if (result!=ORPHEUS_OK) { commit_parameters(s, &before); return result; }
    candidate=after;
    s->_orpheus_cleanup_pending=true;
    commit_parameters(s, &candidate); return ORPHEUS_OK;
}
static int get_parameter(void* state, const char* key, OrpheusValue* value) {
    orpheus_builtin_fade_State* s=(orpheus_builtin_fade_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "fade")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->fade; return ORPHEUS_OK;
    }
    if (strcmp(key, "crossover_hz")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->crossover_hz; return ORPHEUS_OK;
    }
    if (strcmp(key, "channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->channels; return ORPHEUS_OK;
    }
    if (strcmp(key, "front_channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->front_channels; return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp_ms")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->ramp_ms; return ORPHEUS_OK;
    }
    if (strcmp(key, "execution_mode")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->execution_mode; return ORPHEUS_OK;
    }
    if (strcmp(key, "chan_map")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->chan_map; return ORPHEUS_OK;
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
    return ORPHEUS_ERR_NOT_FOUND;
}
static int configure_parameters(orpheus_builtin_fade_Parameters* p, const OrpheusConfig* config) {
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
    return orpheus_builtin_fade_validate(p);
}
static int cleanup(orpheus_builtin_fade_State* s) {
    int result=ORPHEUS_OK;
    if (s->_orpheus_cleanup_pending) {
        s->_orpheus_cleanup_pending=false;
        memset(&s->algorithm, 0, sizeof(s->algorithm));
    }
    return result;
}
static int prepare(void* state, const OrpheusConfig* config) {
    orpheus_builtin_fade_State* s=(orpheus_builtin_fade_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_fade_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    result=cleanup(s); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate);
    s->_orpheus_cleanup_pending=true;
    result=orpheus_builtin_fade_prepare(s, config);
    if (result!=ORPHEUS_OK) { (void)cleanup(s); return result; }
    return ORPHEUS_OK;
}
static int create(void** state, const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=NULL;
    if (!config || !config->state_block) return ORPHEUS_ERR_INVALID_ARG;
    if ((uintptr_t)config->state_block % descriptor.alignment) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_fade_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    orpheus_builtin_fade_State* s=(orpheus_builtin_fade_State*)config->state_block;
    memset(s, 0, sizeof(*s)); commit_parameters(s, &candidate);
    s->_orpheus_alive=true; *state=s; return ORPHEUS_OK;
}
static int destroy(void* state) {
    orpheus_builtin_fade_State* s=(orpheus_builtin_fade_State*)state;
    if (!s) return ORPHEUS_ERR_INVALID_ARG;
    if (!s->_orpheus_alive) return ORPHEUS_OK;
    int result=cleanup(s); s->_orpheus_alive=false; return result;
}
static int reset(void* state) {
    orpheus_builtin_fade_State* s=(orpheus_builtin_fade_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_builtin_fade_reset(s);
}
int orpheus_builtin_fade_register_current_gains(orpheus_builtin_fade_State* s, const OrpheusRegistry* reg) {
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_fade_Parameters current; snapshot_parameters(s, &current);
    uint32_t count_current_gains;
    if (resource_count_current_gains(&current, &count_current_gains)!=ORPHEUS_OK) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_ARRAY(reg, s, algorithm.current_gains, count_current_gains, ORPHEUS_SLOT_BULK, "current_gains", "高频前后/低频前后当前增益", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_DOUBLE_BUFFERED)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    return ORPHEUS_OK;
}
int orpheus_builtin_fade_register_target_gains(orpheus_builtin_fade_State* s, const OrpheusRegistry* reg) {
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_fade_Parameters current; snapshot_parameters(s, &current);
    uint32_t count_target_gains;
    if (resource_count_target_gains(&current, &count_target_gains)!=ORPHEUS_OK) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_ARRAY(reg, s, algorithm.target_gains, count_target_gains, ORPHEUS_SLOT_BULK, "target_gains", "高频前后/低频前后目标增益", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_DOUBLE_BUFFERED)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    return ORPHEUS_OK;
}
int orpheus_builtin_fade_register_coefficients(orpheus_builtin_fade_State* s, const OrpheusRegistry* reg) {
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_fade_Parameters current; snapshot_parameters(s, &current);
    uint32_t count_coefficients;
    if (resource_count_coefficients(&current, &count_coefficients)!=ORPHEUS_OK) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_ARRAY(reg, s, algorithm.coefficients, count_coefficients, ORPHEUS_SLOT_BULK, "coefficients", "高频前后/低频前后幂次乘数", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_DOUBLE_BUFFERED)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    return ORPHEUS_OK;
}
int orpheus_builtin_fade_register_remaining_blocks(orpheus_builtin_fade_State* s, const OrpheusRegistry* reg) {
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_fade_Parameters current; snapshot_parameters(s, &current);
    uint32_t count_remaining_blocks;
    if (resource_count_remaining_blocks(&current, &count_remaining_blocks)!=ORPHEUS_OK) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_ARRAY(reg, s, algorithm.remaining_blocks, count_remaining_blocks, ORPHEUS_SLOT_BULK, "remaining_blocks", "四组剩余块数（仅前两组驱动）", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_DOUBLE_BUFFERED, .min_i32=(-2147483647 - 1), .max_i32=2147483647)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    return ORPHEUS_OK;
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    orpheus_builtin_fade_State* s=(orpheus_builtin_fade_State*)state;
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_SLOT(reg, s, fade, ORPHEUS_SLOT_SETTING, "fade", "衰减", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=-1.000000000e+00f, .max_f32=1.000000000e+00f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, crossover_hz, ORPHEUS_SLOT_SETTING, "crossover_hz", "分频点", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="Hz", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=1.000000000e+02f, .max_f32=2.000000000e+03f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=2, .max_i32=32)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, front_channels, ORPHEUS_SLOT_SETTING, "front_channels", "前组通道数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_i32=1, .max_i32=31)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, ramp_ms, ORPHEUS_SLOT_SETTING, "ramp_ms", "斜坡时间", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="ms", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=0.000000000e+00f, .max_f32=1.000000000e+03f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, execution_mode, ORPHEUS_SLOT_SETTING, "execution_mode", "执行模式（0内部分频，1外部分频显式包络）", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=0, .max_i32=1)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, chan_map, ORPHEUS_SLOT_SETTING, "chan_map", "通道到前后组映射（-1正零）", ORPHEUS_VALUE_STRING, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    int result_current_gains=orpheus_builtin_fade_register_current_gains(s, reg);
    if (result_current_gains!=ORPHEUS_OK) return result_current_gains;
    int result_target_gains=orpheus_builtin_fade_register_target_gains(s, reg);
    if (result_target_gains!=ORPHEUS_OK) return result_target_gains;
    int result_coefficients=orpheus_builtin_fade_register_coefficients(s, reg);
    if (result_coefficients!=ORPHEUS_OK) return result_coefficients;
    int result_remaining_blocks=orpheus_builtin_fade_register_remaining_blocks(s, reg);
    if (result_remaining_blocks!=ORPHEUS_OK) return result_remaining_blocks;
    if (ORPHEUS_REG_SLOT(reg, s, freeze, ORPHEUS_SLOT_SETTING, "freeze", "冻结显式包络", ORPHEUS_VALUE_BOOL, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
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
    orpheus_builtin_fade_State* s=(orpheus_builtin_fade_State*)state;
    orpheus_builtin_fade_IO io={0};
    if (!s || !s->_orpheus_alive || !ctx) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t _orpheus_input_count=0, _orpheus_output_count=0;
    _orpheus_input_count+=1;
    _orpheus_output_count+=1;
    int64_t _orpheus_count_lpf=s->execution_mode;
    if (_orpheus_count_lpf < 0 || _orpheus_count_lpf > 1)
        return ORPHEUS_ERR_INVALID_ARG;
    io.lpf_count=(uint32_t)_orpheus_count_lpf;
    _orpheus_input_count+=io.lpf_count;
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
    for (uint32_t _orpheus_i=0; _orpheus_i<io.lpf_count; ++_orpheus_i) {
    io.lpf[_orpheus_i].input_index=_orpheus_input_slot+_orpheus_i;
    io.lpf[_orpheus_i].buffer=(ctx->inputs && ctx->input_count > (_orpheus_input_slot+_orpheus_i)) ? ctx->inputs[_orpheus_input_slot+_orpheus_i] : NULL;
    if (!valid_buffer(io.lpf[_orpheus_i].buffer, (uint32_t)(s->channels),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.lpf[_orpheus_i].samples=io.lpf[_orpheus_i].buffer ? (const float*)io.lpf[_orpheus_i].buffer->data : NULL;
    }
    _orpheus_input_slot+=io.lpf_count;
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
    return orpheus_builtin_fade_algorithm(s, &io, ctx);
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
