#include "component_state.h"
#include <string.h>
#include <math.h>

_Static_assert(_Generic(&(((orpheus_builtin_limiter_State*)0)->algorithm.feedback_attack), float (*)[64]: 1, default: 0),
    "BULK feedback_attack: storage 类型或固定容量不匹配");
_Static_assert(_Generic(&(((orpheus_builtin_limiter_State*)0)->algorithm.feedback_release), float (*)[64]: 1, default: 0),
    "BULK feedback_release: storage 类型或固定容量不匹配");
_Static_assert(_Generic(&(((orpheus_builtin_limiter_State*)0)->algorithm.feedback_k1), float (*)[64]: 1, default: 0),
    "BULK feedback_k1: storage 类型或固定容量不匹配");
_Static_assert(_Generic(&(((orpheus_builtin_limiter_State*)0)->algorithm.feedback_min_factor), float (*)[64]: 1, default: 0),
    "BULK feedback_min_factor: storage 类型或固定容量不匹配");
static const OrpheusPort ports[] = {
    {.id="in", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
};

static const OrpheusParameter params[] = {
    {.id="mode", .name="模式", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str="shared"}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="threshold_db", .name="阈值", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=-6.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="dB", .readback=true, .persistent=true, .affects_signature=false, .min_f32=-9.600000000e+01f, .max_f32=2.400000000e+01f},
    {.id="attack_ms", .name="启动时间", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=5.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="ms", .readback=true, .persistent=true, .affects_signature=false, .min_f32=1.000000000e-01f, .max_f32=1.000000000e+03f},
    {.id="release_ms", .name="释放时间", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=1.000000000e+02f}, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="ms", .readback=true, .persistent=true, .affects_signature=false, .min_f32=1.000000000e+00f, .max_f32=5.000000000e+03f},
    {.id="attack_coeffs", .name="每通道启动系数", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str="0.0237"}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="release_coeffs", .name="每通道释放系数", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str="1.00024"}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="k1", .name="每通道 k1", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str="0.01185"}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="max_attack", .name="每通道最大衰减", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str="0.31623"}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="channels", .name="通道数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=2}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=1, .max_i32=32},
    {.id="parameter_set", .name="反馈参数组", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=1}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_i32=1, .max_i32=2},
    {.id="disabled", .name="反馈限幅旁路", .type=ORPHEUS_VALUE_BOOL, .default_value={.type=ORPHEUS_VALUE_BOOL, .value.b=false}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="feedback_attack", .name="反馈启动系数（两组）", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str=""}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="feedback_release", .name="反馈释放乘数（两组）", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str=""}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="feedback_k1", .name="反馈检测系数（两组）", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str=""}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="feedback_min_factor", .name="反馈最小乘数（两组）", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str=""}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="active_mask", .name="本块限幅通道位图", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=0}, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .readback=true, .persistent=false, .affects_signature=false},
    {.id="gain", .name="最后通道当前增益", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=1.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .readback=true, .persistent=false, .affects_signature=false},
};

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.limiter", .version="1.2.0", .abi_version=7,
    .ports=ports, .port_count=2,
    .params=params, .param_count=17,
    .state_size=sizeof(orpheus_builtin_limiter_State), .alignment=_Alignof(orpheus_builtin_limiter_State),
    .scratch_size=0, .latency_samples=0,
    .realtime_safe=true, .supports_inplace=true
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

static void snapshot_parameters(const orpheus_builtin_limiter_State* s, orpheus_builtin_limiter_Parameters* p) {
    (void)s; memset(p, 0, sizeof(*p));
    memcpy(p->mode, s->mode, sizeof(p->mode));
    p->threshold_db=s->threshold_db;
    p->attack_ms=s->attack_ms;
    p->release_ms=s->release_ms;
    memcpy(p->attack_coeffs, s->attack_coeffs, sizeof(p->attack_coeffs));
    memcpy(p->release_coeffs, s->release_coeffs, sizeof(p->release_coeffs));
    memcpy(p->k1, s->k1, sizeof(p->k1));
    memcpy(p->max_attack, s->max_attack, sizeof(p->max_attack));
    p->channels=s->channels;
    p->parameter_set=s->parameter_set;
    p->disabled=s->disabled;
    memcpy(p->feedback_attack, s->feedback_attack, sizeof(p->feedback_attack));
    memcpy(p->feedback_release, s->feedback_release, sizeof(p->feedback_release));
    memcpy(p->feedback_k1, s->feedback_k1, sizeof(p->feedback_k1));
    memcpy(p->feedback_min_factor, s->feedback_min_factor, sizeof(p->feedback_min_factor));
    p->active_mask=s->active_mask;
    p->gain=s->gain;
}
static void commit_parameters(orpheus_builtin_limiter_State* s, const orpheus_builtin_limiter_Parameters* p) {
    (void)s; (void)p;
    memcpy(s->mode, p->mode, sizeof(s->mode));
    s->threshold_db=p->threshold_db;
    s->attack_ms=p->attack_ms;
    s->release_ms=p->release_ms;
    memcpy(s->attack_coeffs, p->attack_coeffs, sizeof(s->attack_coeffs));
    memcpy(s->release_coeffs, p->release_coeffs, sizeof(s->release_coeffs));
    memcpy(s->k1, p->k1, sizeof(s->k1));
    memcpy(s->max_attack, p->max_attack, sizeof(s->max_attack));
    s->channels=p->channels;
    s->parameter_set=p->parameter_set;
    s->disabled=p->disabled;
    memcpy(s->feedback_attack, p->feedback_attack, sizeof(s->feedback_attack));
    memcpy(s->feedback_release, p->feedback_release, sizeof(s->feedback_release));
    memcpy(s->feedback_k1, p->feedback_k1, sizeof(s->feedback_k1));
    memcpy(s->feedback_min_factor, p->feedback_min_factor, sizeof(s->feedback_min_factor));
    s->active_mask=p->active_mask;
    s->gain=p->gain;
}
static void default_parameters(orpheus_builtin_limiter_Parameters* p) {
    memset(p, 0, sizeof(*p));
    memcpy(p->mode, "shared", 7);
    p->threshold_db=-6.000000000e+00f;
    p->attack_ms=5.000000000e+00f;
    p->release_ms=1.000000000e+02f;
    memcpy(p->attack_coeffs, "0.0237", 7);
    memcpy(p->release_coeffs, "1.00024", 8);
    memcpy(p->k1, "0.01185", 8);
    memcpy(p->max_attack, "0.31623", 8);
    p->channels=2;
    p->parameter_set=1;
    p->disabled=false;
    memcpy(p->feedback_attack, "", 1);
    memcpy(p->feedback_release, "", 1);
    memcpy(p->feedback_k1, "", 1);
    memcpy(p->feedback_min_factor, "", 1);
    p->active_mask=0;
    p->gain=1.000000000e+00f;
}
static int resource_count_feedback_attack(const orpheus_builtin_limiter_Parameters* p, uint32_t* count) {
    (void)p; uint32_t product=1;
    int64_t dimension0=2;
    if (dimension0<1 || (uint64_t)dimension0>64u/product) return ORPHEUS_ERR_INVALID_ARG;
    product*=(uint32_t)dimension0;
    int64_t dimension1=p->channels;
    if (dimension1<1 || (uint64_t)dimension1>64u/product) return ORPHEUS_ERR_INVALID_ARG;
    product*=(uint32_t)dimension1;
    *count=product; return ORPHEUS_OK;
}
static int resource_count_feedback_release(const orpheus_builtin_limiter_Parameters* p, uint32_t* count) {
    (void)p; uint32_t product=1;
    int64_t dimension0=2;
    if (dimension0<1 || (uint64_t)dimension0>64u/product) return ORPHEUS_ERR_INVALID_ARG;
    product*=(uint32_t)dimension0;
    int64_t dimension1=p->channels;
    if (dimension1<1 || (uint64_t)dimension1>64u/product) return ORPHEUS_ERR_INVALID_ARG;
    product*=(uint32_t)dimension1;
    *count=product; return ORPHEUS_OK;
}
static int resource_count_feedback_k1(const orpheus_builtin_limiter_Parameters* p, uint32_t* count) {
    (void)p; uint32_t product=1;
    int64_t dimension0=2;
    if (dimension0<1 || (uint64_t)dimension0>64u/product) return ORPHEUS_ERR_INVALID_ARG;
    product*=(uint32_t)dimension0;
    int64_t dimension1=p->channels;
    if (dimension1<1 || (uint64_t)dimension1>64u/product) return ORPHEUS_ERR_INVALID_ARG;
    product*=(uint32_t)dimension1;
    *count=product; return ORPHEUS_OK;
}
static int resource_count_feedback_min_factor(const orpheus_builtin_limiter_Parameters* p, uint32_t* count) {
    (void)p; uint32_t product=1;
    int64_t dimension0=2;
    if (dimension0<1 || (uint64_t)dimension0>64u/product) return ORPHEUS_ERR_INVALID_ARG;
    product*=(uint32_t)dimension0;
    int64_t dimension1=p->channels;
    if (dimension1<1 || (uint64_t)dimension1>64u/product) return ORPHEUS_ERR_INVALID_ARG;
    product*=(uint32_t)dimension1;
    *count=product; return ORPHEUS_OK;
}
static int validate_resource_counts(const orpheus_builtin_limiter_Parameters* p) {
    uint32_t count; int result;
    result=resource_count_feedback_attack(p, &count);
    if (result!=ORPHEUS_OK) return result;
    result=resource_count_feedback_release(p, &count);
    if (result!=ORPHEUS_OK) return result;
    result=resource_count_feedback_k1(p, &count);
    if (result!=ORPHEUS_OK) return result;
    result=resource_count_feedback_min_factor(p, &count);
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
static int update_parameter(orpheus_builtin_limiter_Parameters* p, const char* key, const OrpheusValue* value) {
    if (!p || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
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
    if (strcmp(key, "threshold_db") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<-9.600000000e+01f || value->value.f32>2.400000000e+01f) return ORPHEUS_ERR_INVALID_ARG;
        p->threshold_db=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "attack_ms") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<1.000000000e-01f || value->value.f32>1.000000000e+03f) return ORPHEUS_ERR_INVALID_ARG;
        p->attack_ms=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "release_ms") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<1.000000000e+00f || value->value.f32>5.000000000e+03f) return ORPHEUS_ERR_INVALID_ARG;
        p->release_ms=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "attack_coeffs") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->attack_coeffs), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->attack_coeffs)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->attack_coeffs, text, sizeof(text));
        return ORPHEUS_OK;
    }
    if (strcmp(key, "release_coeffs") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->release_coeffs), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->release_coeffs)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->release_coeffs, text, sizeof(text));
        return ORPHEUS_OK;
    }
    if (strcmp(key, "k1") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->k1), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->k1)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->k1, text, sizeof(text));
        return ORPHEUS_OK;
    }
    if (strcmp(key, "max_attack") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->max_attack), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->max_attack)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->max_attack, text, sizeof(text));
        return ORPHEUS_OK;
    }
    if (strcmp(key, "channels") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>32) return ORPHEUS_ERR_INVALID_ARG;
        p->channels=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "parameter_set") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>2) return ORPHEUS_ERR_INVALID_ARG;
        p->parameter_set=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "disabled") == 0) {
        if (value->type!=ORPHEUS_VALUE_BOOL) return ORPHEUS_ERR_INVALID_ARG;
        p->disabled=value->value.b;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "feedback_attack") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->feedback_attack), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->feedback_attack)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->feedback_attack, text, sizeof(text));
        return ORPHEUS_OK;
    }
    if (strcmp(key, "feedback_release") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->feedback_release), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->feedback_release)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->feedback_release, text, sizeof(text));
        return ORPHEUS_OK;
    }
    if (strcmp(key, "feedback_k1") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->feedback_k1), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->feedback_k1)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->feedback_k1, text, sizeof(text));
        return ORPHEUS_OK;
    }
    if (strcmp(key, "feedback_min_factor") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->feedback_min_factor), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->feedback_min_factor)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->feedback_min_factor, text, sizeof(text));
        return ORPHEUS_OK;
    }
    if (strcmp(key, "active_mask") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        p->active_mask=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "gain") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->gain=value->value.f32;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int parameter_value(const orpheus_builtin_limiter_Parameters* p, const char* key, OrpheusValue* value) {
    (void)p; (void)key; (void)value;
    if (strcmp(key, "mode")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=p->mode; return ORPHEUS_OK;
    }
    if (strcmp(key, "threshold_db")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->threshold_db; return ORPHEUS_OK;
    }
    if (strcmp(key, "attack_ms")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->attack_ms; return ORPHEUS_OK;
    }
    if (strcmp(key, "release_ms")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->release_ms; return ORPHEUS_OK;
    }
    if (strcmp(key, "attack_coeffs")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=p->attack_coeffs; return ORPHEUS_OK;
    }
    if (strcmp(key, "release_coeffs")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=p->release_coeffs; return ORPHEUS_OK;
    }
    if (strcmp(key, "k1")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=p->k1; return ORPHEUS_OK;
    }
    if (strcmp(key, "max_attack")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=p->max_attack; return ORPHEUS_OK;
    }
    if (strcmp(key, "channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->channels; return ORPHEUS_OK;
    }
    if (strcmp(key, "parameter_set")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->parameter_set; return ORPHEUS_OK;
    }
    if (strcmp(key, "disabled")==0) {
        value->type=ORPHEUS_VALUE_BOOL; value->value.b=p->disabled; return ORPHEUS_OK;
    }
    if (strcmp(key, "feedback_attack")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=p->feedback_attack; return ORPHEUS_OK;
    }
    if (strcmp(key, "feedback_release")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=p->feedback_release; return ORPHEUS_OK;
    }
    if (strcmp(key, "feedback_k1")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=p->feedback_k1; return ORPHEUS_OK;
    }
    if (strcmp(key, "feedback_min_factor")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=p->feedback_min_factor; return ORPHEUS_OK;
    }
    if (strcmp(key, "active_mask")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->active_mask; return ORPHEUS_OK;
    }
    if (strcmp(key, "gain")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->gain; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int set_parameter(void* state, const char* key, const OrpheusValue* value) {
    orpheus_builtin_limiter_State* s=(orpheus_builtin_limiter_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "active_mask")==0) return ORPHEUS_ERR_UNSUPPORTED;
    if (strcmp(key, "gain")==0) return ORPHEUS_ERR_UNSUPPORTED;
    orpheus_builtin_limiter_Parameters before, candidate; snapshot_parameters(s, &before); candidate=before;
    int result=update_parameter(&candidate, key, value);
    if (result!=ORPHEUS_OK) return result;
    result=validate_resource_counts(&candidate); if (result!=ORPHEUS_OK) return result;
    result=orpheus_builtin_limiter_validate(&candidate); if (result!=ORPHEUS_OK) return result;
    OrpheusValue checked; result=parameter_value(&candidate, key, &checked);
    if (result!=ORPHEUS_OK) return result;
    result=orpheus_builtin_limiter_set_parameter(s, key, &checked);
    if (result!=ORPHEUS_OK) { commit_parameters(s, &before); return result; }
    orpheus_builtin_limiter_Parameters after; snapshot_parameters(s, &after);
    result=update_parameter(&after, key, &checked);
    if (result!=ORPHEUS_OK) { commit_parameters(s, &before); return result; }
    candidate=after;
    s->_orpheus_cleanup_pending=true;
    commit_parameters(s, &candidate); return ORPHEUS_OK;
}
static int get_parameter(void* state, const char* key, OrpheusValue* value) {
    orpheus_builtin_limiter_State* s=(orpheus_builtin_limiter_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "mode")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->mode; return ORPHEUS_OK;
    }
    if (strcmp(key, "threshold_db")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->threshold_db; return ORPHEUS_OK;
    }
    if (strcmp(key, "attack_ms")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->attack_ms; return ORPHEUS_OK;
    }
    if (strcmp(key, "release_ms")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->release_ms; return ORPHEUS_OK;
    }
    if (strcmp(key, "attack_coeffs")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->attack_coeffs; return ORPHEUS_OK;
    }
    if (strcmp(key, "release_coeffs")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->release_coeffs; return ORPHEUS_OK;
    }
    if (strcmp(key, "k1")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->k1; return ORPHEUS_OK;
    }
    if (strcmp(key, "max_attack")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->max_attack; return ORPHEUS_OK;
    }
    if (strcmp(key, "channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->channels; return ORPHEUS_OK;
    }
    if (strcmp(key, "parameter_set")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->parameter_set; return ORPHEUS_OK;
    }
    if (strcmp(key, "disabled")==0) {
        value->type=ORPHEUS_VALUE_BOOL; value->value.b=s->disabled; return ORPHEUS_OK;
    }
    if (strcmp(key, "feedback_attack")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->feedback_attack; return ORPHEUS_OK;
    }
    if (strcmp(key, "feedback_release")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->feedback_release; return ORPHEUS_OK;
    }
    if (strcmp(key, "feedback_k1")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->feedback_k1; return ORPHEUS_OK;
    }
    if (strcmp(key, "feedback_min_factor")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->feedback_min_factor; return ORPHEUS_OK;
    }
    if (strcmp(key, "active_mask")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->active_mask; return ORPHEUS_OK;
    }
    if (strcmp(key, "gain")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->gain; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int configure_parameters(orpheus_builtin_limiter_Parameters* p, const OrpheusConfig* config) {
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
    return orpheus_builtin_limiter_validate(p);
}
static int cleanup(orpheus_builtin_limiter_State* s) {
    int result=ORPHEUS_OK;
    if (s->_orpheus_cleanup_pending) {
        s->_orpheus_cleanup_pending=false;
        memset(&s->algorithm, 0, sizeof(s->algorithm));
    }
    return result;
}
static int prepare(void* state, const OrpheusConfig* config) {
    orpheus_builtin_limiter_State* s=(orpheus_builtin_limiter_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_limiter_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    result=cleanup(s); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate);
    s->_orpheus_cleanup_pending=true;
    result=orpheus_builtin_limiter_prepare(s, config);
    if (result!=ORPHEUS_OK) { (void)cleanup(s); return result; }
    return ORPHEUS_OK;
}
static int create(void** state, const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=NULL;
    if (!config || !config->state_block) return ORPHEUS_ERR_INVALID_ARG;
    if ((uintptr_t)config->state_block % descriptor.alignment) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_limiter_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    orpheus_builtin_limiter_State* s=(orpheus_builtin_limiter_State*)config->state_block;
    memset(s, 0, sizeof(*s)); commit_parameters(s, &candidate);
    s->_orpheus_alive=true; *state=s; return ORPHEUS_OK;
}
static int destroy(void* state) {
    orpheus_builtin_limiter_State* s=(orpheus_builtin_limiter_State*)state;
    if (!s) return ORPHEUS_ERR_INVALID_ARG;
    if (!s->_orpheus_alive) return ORPHEUS_OK;
    int result=cleanup(s); s->_orpheus_alive=false; return result;
}
static int reset(void* state) {
    orpheus_builtin_limiter_State* s=(orpheus_builtin_limiter_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_builtin_limiter_reset(s);
}
int orpheus_builtin_limiter_register_feedback_attack(orpheus_builtin_limiter_State* s, const OrpheusRegistry* reg) {
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_limiter_Parameters current; snapshot_parameters(s, &current);
    uint32_t count_feedback_attack;
    if (resource_count_feedback_attack(&current, &count_feedback_attack)!=ORPHEUS_OK) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_ARRAY(reg, s, algorithm.feedback_attack, count_feedback_attack, ORPHEUS_SLOT_BULK, "feedback_attack", "反馈启动系数（两组）", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_DOUBLE_BUFFERED)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    return ORPHEUS_OK;
}
int orpheus_builtin_limiter_register_feedback_release(orpheus_builtin_limiter_State* s, const OrpheusRegistry* reg) {
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_limiter_Parameters current; snapshot_parameters(s, &current);
    uint32_t count_feedback_release;
    if (resource_count_feedback_release(&current, &count_feedback_release)!=ORPHEUS_OK) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_ARRAY(reg, s, algorithm.feedback_release, count_feedback_release, ORPHEUS_SLOT_BULK, "feedback_release", "反馈释放乘数（两组）", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_DOUBLE_BUFFERED)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    return ORPHEUS_OK;
}
int orpheus_builtin_limiter_register_feedback_k1(orpheus_builtin_limiter_State* s, const OrpheusRegistry* reg) {
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_limiter_Parameters current; snapshot_parameters(s, &current);
    uint32_t count_feedback_k1;
    if (resource_count_feedback_k1(&current, &count_feedback_k1)!=ORPHEUS_OK) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_ARRAY(reg, s, algorithm.feedback_k1, count_feedback_k1, ORPHEUS_SLOT_BULK, "feedback_k1", "反馈检测系数（两组）", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_DOUBLE_BUFFERED)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    return ORPHEUS_OK;
}
int orpheus_builtin_limiter_register_feedback_min_factor(orpheus_builtin_limiter_State* s, const OrpheusRegistry* reg) {
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_limiter_Parameters current; snapshot_parameters(s, &current);
    uint32_t count_feedback_min_factor;
    if (resource_count_feedback_min_factor(&current, &count_feedback_min_factor)!=ORPHEUS_OK) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_ARRAY(reg, s, algorithm.feedback_min_factor, count_feedback_min_factor, ORPHEUS_SLOT_BULK, "feedback_min_factor", "反馈最小乘数（两组）", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_DOUBLE_BUFFERED)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    return ORPHEUS_OK;
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    orpheus_builtin_limiter_State* s=(orpheus_builtin_limiter_State*)state;
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_SLOT(reg, s, mode, ORPHEUS_SLOT_SETTING, "mode", "模式", ORPHEUS_VALUE_STRING, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, threshold_db, ORPHEUS_SLOT_SETTING, "threshold_db", "阈值", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="dB", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=-9.600000000e+01f, .max_f32=2.400000000e+01f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, attack_ms, ORPHEUS_SLOT_SETTING, "attack_ms", "启动时间", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="ms", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=1.000000000e-01f, .max_f32=1.000000000e+03f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, release_ms, ORPHEUS_SLOT_SETTING, "release_ms", "释放时间", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="ms", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=1.000000000e+00f, .max_f32=5.000000000e+03f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, attack_coeffs, ORPHEUS_SLOT_SETTING, "attack_coeffs", "每通道启动系数", ORPHEUS_VALUE_STRING, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, release_coeffs, ORPHEUS_SLOT_SETTING, "release_coeffs", "每通道释放系数", ORPHEUS_VALUE_STRING, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, k1, ORPHEUS_SLOT_SETTING, "k1", "每通道 k1", ORPHEUS_VALUE_STRING, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, max_attack, ORPHEUS_SLOT_SETTING, "max_attack", "每通道最大衰减", ORPHEUS_VALUE_STRING, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=1, .max_i32=32)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, parameter_set, ORPHEUS_SLOT_SETTING, "parameter_set", "反馈参数组", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_i32=1, .max_i32=2)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, disabled, ORPHEUS_SLOT_SETTING, "disabled", "反馈限幅旁路", ORPHEUS_VALUE_BOOL, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    int result_feedback_attack=orpheus_builtin_limiter_register_feedback_attack(s, reg);
    if (result_feedback_attack!=ORPHEUS_OK) return result_feedback_attack;
    int result_feedback_release=orpheus_builtin_limiter_register_feedback_release(s, reg);
    if (result_feedback_release!=ORPHEUS_OK) return result_feedback_release;
    int result_feedback_k1=orpheus_builtin_limiter_register_feedback_k1(s, reg);
    if (result_feedback_k1!=ORPHEUS_OK) return result_feedback_k1;
    int result_feedback_min_factor=orpheus_builtin_limiter_register_feedback_min_factor(s, reg);
    if (result_feedback_min_factor!=ORPHEUS_OK) return result_feedback_min_factor;
    if (ORPHEUS_REG_SLOT(reg, s, active_mask, ORPHEUS_SLOT_PROBE, "active_mask", "本块限幅通道位图", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .flags=ORPHEUS_SLOT_READBACK)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, gain, ORPHEUS_SLOT_PROBE, "gain", "最后通道当前增益", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .flags=ORPHEUS_SLOT_READBACK)==ORPHEUS_SLOT_ID_INVALID)
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
    orpheus_builtin_limiter_State* s=(orpheus_builtin_limiter_State*)state;
    orpheus_builtin_limiter_IO io={0};
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
    return orpheus_builtin_limiter_algorithm(s, &io, ctx);
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
