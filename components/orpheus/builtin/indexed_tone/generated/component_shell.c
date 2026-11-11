#include "component_state.h"
#include <string.h>
#include <math.h>

_Static_assert(_Generic(&(((orpheus_builtin_indexed_tone_State*)0)->algorithm.boost_coefs), float (*)[5]: 1, default: 0),
    "BULK boost_coefs: storage 类型或固定容量不匹配");
_Static_assert(_Generic(&(((orpheus_builtin_indexed_tone_State*)0)->algorithm.cut_coefs), float (*)[5]: 1, default: 0),
    "BULK cut_coefs: storage 类型或固定容量不匹配");
static const OrpheusPort ports[] = {
    {.id="in", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
};

static const OrpheusParameter params[] = {
    {.id="channels", .name="通道数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=2}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=1, .max_i32=64},
    {.id="gain_index", .name="增益索引", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=1.280000000e+02f}, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_f32=0.000000000e+00f, .max_f32=2.550000000e+02f},
    {.id="offset", .name="中心索引", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=1.280000000e+02f}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="scale", .name="索引缩放", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=1.270000000e+02f}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="max_boost_db", .name="最大提升", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=2.000000000e+01f}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="dB", .readback=true, .persistent=true, .affects_signature=false},
    {.id="max_cut_db", .name="最大衰减", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=2.000000000e+01f}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="dB", .readback=true, .persistent=true, .affects_signature=false},
    {.id="ramp_ms", .name="斜坡时间", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=3.000000000e+01f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="ms", .readback=true, .persistent=true, .affects_signature=false, .min_f32=0.000000000e+00f, .max_f32=3.000000000e+03f},
    {.id="ramp_db_per_s", .name="斜坡速率", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="dB/s", .readback=true, .persistent=true, .affects_signature=false, .min_f32=0.000000000e+00f, .max_f32=5.000000000e+04f},
    {.id="freeze", .name="冻结斜坡", .type=ORPHEUS_VALUE_BOOL, .default_value={.type=ORPHEUS_VALUE_BOOL, .value.b=false}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="filter_order", .name="滤波阶数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=1}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_i32=1, .max_i32=2},
    {.id="boost_coefs", .name="Boost原始系数", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str="1,0,0,0,0"}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="cut_coefs", .name="Cut原始系数", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str="1,0,0,0,0"}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="execution_mode", .name="执行模式（0索引请求，1显式两阶段）", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=0}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_i32=0, .max_i32=1},
    {.id="phase", .name="当前阶段", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=1}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_i32=1, .max_i32=2},
    {.id="ramp0_current_gain", .name="阶段0当前线性增益", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=1.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="ramp0_target_gain", .name="阶段0目标线性增益", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=1.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="ramp0_coefficient", .name="阶段0逐样本乘数", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=1.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="ramp0_remaining_blocks", .name="阶段0剩余块数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=0}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_i32=(-2147483647 - 1), .max_i32=2147483647},
    {.id="ramp1_current_gain", .name="阶段1当前线性增益", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=1.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="ramp1_target_gain", .name="阶段1目标线性增益", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=1.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="ramp1_coefficient", .name="阶段1逐样本乘数", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=1.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="ramp1_remaining_blocks", .name="阶段1剩余块数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=0}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_i32=(-2147483647 - 1), .max_i32=2147483647},
};

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.indexed_tone", .version="1.1.0", .abi_version=7,
    .ports=ports, .port_count=2,
    .params=params, .param_count=22,
    .state_size=sizeof(orpheus_builtin_indexed_tone_State), .alignment=_Alignof(orpheus_builtin_indexed_tone_State),
    .scratch_size=0, .latency_samples=0,
    .realtime_safe=true, .supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

static void snapshot_parameters(const orpheus_builtin_indexed_tone_State* s, orpheus_builtin_indexed_tone_Parameters* p) {
    (void)s; memset(p, 0, sizeof(*p));
    p->channels=s->channels;
    p->gain_index=s->gain_index;
    p->offset=s->offset;
    p->scale=s->scale;
    p->max_boost_db=s->max_boost_db;
    p->max_cut_db=s->max_cut_db;
    p->ramp_ms=s->ramp_ms;
    p->ramp_db_per_s=s->ramp_db_per_s;
    p->freeze=s->freeze;
    p->filter_order=s->filter_order;
    memcpy(p->boost_coefs, s->boost_coefs, sizeof(p->boost_coefs));
    memcpy(p->cut_coefs, s->cut_coefs, sizeof(p->cut_coefs));
    p->execution_mode=s->execution_mode;
    p->phase=s->phase;
    p->ramp0_current_gain=s->ramp0_current_gain;
    p->ramp0_target_gain=s->ramp0_target_gain;
    p->ramp0_coefficient=s->ramp0_coefficient;
    p->ramp0_remaining_blocks=s->ramp0_remaining_blocks;
    p->ramp1_current_gain=s->ramp1_current_gain;
    p->ramp1_target_gain=s->ramp1_target_gain;
    p->ramp1_coefficient=s->ramp1_coefficient;
    p->ramp1_remaining_blocks=s->ramp1_remaining_blocks;
}
static void commit_parameters(orpheus_builtin_indexed_tone_State* s, const orpheus_builtin_indexed_tone_Parameters* p) {
    (void)s; (void)p;
    s->channels=p->channels;
    s->gain_index=p->gain_index;
    s->offset=p->offset;
    s->scale=p->scale;
    s->max_boost_db=p->max_boost_db;
    s->max_cut_db=p->max_cut_db;
    s->ramp_ms=p->ramp_ms;
    s->ramp_db_per_s=p->ramp_db_per_s;
    s->freeze=p->freeze;
    s->filter_order=p->filter_order;
    memcpy(s->boost_coefs, p->boost_coefs, sizeof(s->boost_coefs));
    memcpy(s->cut_coefs, p->cut_coefs, sizeof(s->cut_coefs));
    s->execution_mode=p->execution_mode;
    s->phase=p->phase;
    s->ramp0_current_gain=p->ramp0_current_gain;
    s->ramp0_target_gain=p->ramp0_target_gain;
    s->ramp0_coefficient=p->ramp0_coefficient;
    s->ramp0_remaining_blocks=p->ramp0_remaining_blocks;
    s->ramp1_current_gain=p->ramp1_current_gain;
    s->ramp1_target_gain=p->ramp1_target_gain;
    s->ramp1_coefficient=p->ramp1_coefficient;
    s->ramp1_remaining_blocks=p->ramp1_remaining_blocks;
}
static void default_parameters(orpheus_builtin_indexed_tone_Parameters* p) {
    memset(p, 0, sizeof(*p));
    p->channels=2;
    p->gain_index=1.280000000e+02f;
    p->offset=1.280000000e+02f;
    p->scale=1.270000000e+02f;
    p->max_boost_db=2.000000000e+01f;
    p->max_cut_db=2.000000000e+01f;
    p->ramp_ms=3.000000000e+01f;
    p->ramp_db_per_s=0.000000000e+00f;
    p->freeze=false;
    p->filter_order=1;
    memcpy(p->boost_coefs, "1,0,0,0,0", 10);
    memcpy(p->cut_coefs, "1,0,0,0,0", 10);
    p->execution_mode=0;
    p->phase=1;
    p->ramp0_current_gain=1.000000000e+00f;
    p->ramp0_target_gain=1.000000000e+00f;
    p->ramp0_coefficient=1.000000000e+00f;
    p->ramp0_remaining_blocks=0;
    p->ramp1_current_gain=1.000000000e+00f;
    p->ramp1_target_gain=1.000000000e+00f;
    p->ramp1_coefficient=1.000000000e+00f;
    p->ramp1_remaining_blocks=0;
}
static int resource_count_boost_coefs(const orpheus_builtin_indexed_tone_Parameters* p, uint32_t* count) {
    (void)p; uint32_t product=1;
    int64_t dimension0=5;
    if (dimension0<1 || (uint64_t)dimension0>5u/product) return ORPHEUS_ERR_INVALID_ARG;
    product*=(uint32_t)dimension0;
    *count=product; return ORPHEUS_OK;
}
static int resource_count_cut_coefs(const orpheus_builtin_indexed_tone_Parameters* p, uint32_t* count) {
    (void)p; uint32_t product=1;
    int64_t dimension0=5;
    if (dimension0<1 || (uint64_t)dimension0>5u/product) return ORPHEUS_ERR_INVALID_ARG;
    product*=(uint32_t)dimension0;
    *count=product; return ORPHEUS_OK;
}
static int validate_resource_counts(const orpheus_builtin_indexed_tone_Parameters* p) {
    uint32_t count; int result;
    result=resource_count_boost_coefs(p, &count);
    if (result!=ORPHEUS_OK) return result;
    result=resource_count_cut_coefs(p, &count);
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
static int update_parameter(orpheus_builtin_indexed_tone_Parameters* p, const char* key, const OrpheusValue* value) {
    if (!p || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "channels") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>64) return ORPHEUS_ERR_INVALID_ARG;
        p->channels=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "gain_index") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<0.000000000e+00f || value->value.f32>2.550000000e+02f) return ORPHEUS_ERR_INVALID_ARG;
        p->gain_index=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "offset") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->offset=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "scale") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->scale=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "max_boost_db") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->max_boost_db=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "max_cut_db") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->max_cut_db=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp_ms") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<0.000000000e+00f || value->value.f32>3.000000000e+03f) return ORPHEUS_ERR_INVALID_ARG;
        p->ramp_ms=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp_db_per_s") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<0.000000000e+00f || value->value.f32>5.000000000e+04f) return ORPHEUS_ERR_INVALID_ARG;
        p->ramp_db_per_s=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "freeze") == 0) {
        if (value->type!=ORPHEUS_VALUE_BOOL) return ORPHEUS_ERR_INVALID_ARG;
        p->freeze=value->value.b;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "filter_order") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>2) return ORPHEUS_ERR_INVALID_ARG;
        p->filter_order=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "boost_coefs") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->boost_coefs), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->boost_coefs)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->boost_coefs, text, sizeof(text));
        return ORPHEUS_OK;
    }
    if (strcmp(key, "cut_coefs") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->cut_coefs), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->cut_coefs)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->cut_coefs, text, sizeof(text));
        return ORPHEUS_OK;
    }
    if (strcmp(key, "execution_mode") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<0 || value->value.i32>1) return ORPHEUS_ERR_INVALID_ARG;
        p->execution_mode=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "phase") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>2) return ORPHEUS_ERR_INVALID_ARG;
        p->phase=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp0_current_gain") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->ramp0_current_gain=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp0_target_gain") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->ramp0_target_gain=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp0_coefficient") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->ramp0_coefficient=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp0_remaining_blocks") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<(-2147483647 - 1) || value->value.i32>2147483647) return ORPHEUS_ERR_INVALID_ARG;
        p->ramp0_remaining_blocks=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp1_current_gain") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->ramp1_current_gain=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp1_target_gain") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->ramp1_target_gain=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp1_coefficient") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->ramp1_coefficient=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp1_remaining_blocks") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<(-2147483647 - 1) || value->value.i32>2147483647) return ORPHEUS_ERR_INVALID_ARG;
        p->ramp1_remaining_blocks=value->value.i32;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int parameter_value(const orpheus_builtin_indexed_tone_Parameters* p, const char* key, OrpheusValue* value) {
    (void)p; (void)key; (void)value;
    if (strcmp(key, "channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->channels; return ORPHEUS_OK;
    }
    if (strcmp(key, "gain_index")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->gain_index; return ORPHEUS_OK;
    }
    if (strcmp(key, "offset")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->offset; return ORPHEUS_OK;
    }
    if (strcmp(key, "scale")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->scale; return ORPHEUS_OK;
    }
    if (strcmp(key, "max_boost_db")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->max_boost_db; return ORPHEUS_OK;
    }
    if (strcmp(key, "max_cut_db")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->max_cut_db; return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp_ms")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->ramp_ms; return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp_db_per_s")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->ramp_db_per_s; return ORPHEUS_OK;
    }
    if (strcmp(key, "freeze")==0) {
        value->type=ORPHEUS_VALUE_BOOL; value->value.b=p->freeze; return ORPHEUS_OK;
    }
    if (strcmp(key, "filter_order")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->filter_order; return ORPHEUS_OK;
    }
    if (strcmp(key, "boost_coefs")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=p->boost_coefs; return ORPHEUS_OK;
    }
    if (strcmp(key, "cut_coefs")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=p->cut_coefs; return ORPHEUS_OK;
    }
    if (strcmp(key, "execution_mode")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->execution_mode; return ORPHEUS_OK;
    }
    if (strcmp(key, "phase")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->phase; return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp0_current_gain")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->ramp0_current_gain; return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp0_target_gain")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->ramp0_target_gain; return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp0_coefficient")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->ramp0_coefficient; return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp0_remaining_blocks")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->ramp0_remaining_blocks; return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp1_current_gain")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->ramp1_current_gain; return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp1_target_gain")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->ramp1_target_gain; return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp1_coefficient")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->ramp1_coefficient; return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp1_remaining_blocks")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->ramp1_remaining_blocks; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int set_parameter(void* state, const char* key, const OrpheusValue* value) {
    orpheus_builtin_indexed_tone_State* s=(orpheus_builtin_indexed_tone_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_indexed_tone_Parameters before, candidate; snapshot_parameters(s, &before); candidate=before;
    int result=update_parameter(&candidate, key, value);
    if (result!=ORPHEUS_OK) return result;
    result=validate_resource_counts(&candidate); if (result!=ORPHEUS_OK) return result;
    result=orpheus_builtin_indexed_tone_validate(&candidate); if (result!=ORPHEUS_OK) return result;
    OrpheusValue checked; result=parameter_value(&candidate, key, &checked);
    if (result!=ORPHEUS_OK) return result;
    result=orpheus_builtin_indexed_tone_set_parameter(s, key, &checked);
    if (result!=ORPHEUS_OK) { commit_parameters(s, &before); return result; }
    orpheus_builtin_indexed_tone_Parameters after; snapshot_parameters(s, &after);
    result=update_parameter(&after, key, &checked);
    if (result!=ORPHEUS_OK) { commit_parameters(s, &before); return result; }
    candidate=after;
    s->_orpheus_cleanup_pending=true;
    commit_parameters(s, &candidate); return ORPHEUS_OK;
}
static int get_parameter(void* state, const char* key, OrpheusValue* value) {
    orpheus_builtin_indexed_tone_State* s=(orpheus_builtin_indexed_tone_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->channels; return ORPHEUS_OK;
    }
    if (strcmp(key, "gain_index")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->gain_index; return ORPHEUS_OK;
    }
    if (strcmp(key, "offset")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->offset; return ORPHEUS_OK;
    }
    if (strcmp(key, "scale")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->scale; return ORPHEUS_OK;
    }
    if (strcmp(key, "max_boost_db")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->max_boost_db; return ORPHEUS_OK;
    }
    if (strcmp(key, "max_cut_db")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->max_cut_db; return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp_ms")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->ramp_ms; return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp_db_per_s")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->ramp_db_per_s; return ORPHEUS_OK;
    }
    if (strcmp(key, "freeze")==0) {
        value->type=ORPHEUS_VALUE_BOOL; value->value.b=s->freeze; return ORPHEUS_OK;
    }
    if (strcmp(key, "filter_order")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->filter_order; return ORPHEUS_OK;
    }
    if (strcmp(key, "boost_coefs")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->boost_coefs; return ORPHEUS_OK;
    }
    if (strcmp(key, "cut_coefs")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->cut_coefs; return ORPHEUS_OK;
    }
    if (strcmp(key, "execution_mode")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->execution_mode; return ORPHEUS_OK;
    }
    if (strcmp(key, "phase")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->phase; return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp0_current_gain")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->ramp0_current_gain; return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp0_target_gain")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->ramp0_target_gain; return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp0_coefficient")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->ramp0_coefficient; return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp0_remaining_blocks")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->ramp0_remaining_blocks; return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp1_current_gain")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->ramp1_current_gain; return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp1_target_gain")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->ramp1_target_gain; return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp1_coefficient")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->ramp1_coefficient; return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp1_remaining_blocks")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->ramp1_remaining_blocks; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int configure_parameters(orpheus_builtin_indexed_tone_Parameters* p, const OrpheusConfig* config) {
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
    return orpheus_builtin_indexed_tone_validate(p);
}
static int cleanup(orpheus_builtin_indexed_tone_State* s) {
    int result=ORPHEUS_OK;
    if (s->_orpheus_cleanup_pending) {
        s->_orpheus_cleanup_pending=false;
        memset(&s->algorithm, 0, sizeof(s->algorithm));
    }
    return result;
}
static int prepare(void* state, const OrpheusConfig* config) {
    orpheus_builtin_indexed_tone_State* s=(orpheus_builtin_indexed_tone_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_indexed_tone_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    result=cleanup(s); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate);
    s->_orpheus_cleanup_pending=true;
    result=orpheus_builtin_indexed_tone_prepare(s, config);
    if (result!=ORPHEUS_OK) { (void)cleanup(s); return result; }
    return ORPHEUS_OK;
}
static int create(void** state, const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=NULL;
    if (!config || !config->state_block) return ORPHEUS_ERR_INVALID_ARG;
    if ((uintptr_t)config->state_block % descriptor.alignment) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_indexed_tone_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    orpheus_builtin_indexed_tone_State* s=(orpheus_builtin_indexed_tone_State*)config->state_block;
    memset(s, 0, sizeof(*s)); commit_parameters(s, &candidate);
    s->_orpheus_alive=true; *state=s; return ORPHEUS_OK;
}
static int destroy(void* state) {
    orpheus_builtin_indexed_tone_State* s=(orpheus_builtin_indexed_tone_State*)state;
    if (!s) return ORPHEUS_ERR_INVALID_ARG;
    if (!s->_orpheus_alive) return ORPHEUS_OK;
    int result=cleanup(s); s->_orpheus_alive=false; return result;
}
static int reset(void* state) {
    orpheus_builtin_indexed_tone_State* s=(orpheus_builtin_indexed_tone_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_builtin_indexed_tone_reset(s);
}
int orpheus_builtin_indexed_tone_register_boost_coefs(orpheus_builtin_indexed_tone_State* s, const OrpheusRegistry* reg) {
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_indexed_tone_Parameters current; snapshot_parameters(s, &current);
    uint32_t count_boost_coefs;
    if (resource_count_boost_coefs(&current, &count_boost_coefs)!=ORPHEUS_OK) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_ARRAY(reg, s, algorithm.boost_coefs, count_boost_coefs, ORPHEUS_SLOT_BULK, "boost_coefs", "Boost原始系数", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_DOUBLE_BUFFERED)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    return ORPHEUS_OK;
}
int orpheus_builtin_indexed_tone_register_cut_coefs(orpheus_builtin_indexed_tone_State* s, const OrpheusRegistry* reg) {
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_indexed_tone_Parameters current; snapshot_parameters(s, &current);
    uint32_t count_cut_coefs;
    if (resource_count_cut_coefs(&current, &count_cut_coefs)!=ORPHEUS_OK) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_ARRAY(reg, s, algorithm.cut_coefs, count_cut_coefs, ORPHEUS_SLOT_BULK, "cut_coefs", "Cut原始系数", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_DOUBLE_BUFFERED)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    return ORPHEUS_OK;
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    orpheus_builtin_indexed_tone_State* s=(orpheus_builtin_indexed_tone_State*)state;
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=1, .max_i32=64)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, gain_index, ORPHEUS_SLOT_SETTING, "gain_index", "增益索引", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=0.000000000e+00f, .max_f32=2.550000000e+02f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, offset, ORPHEUS_SLOT_SETTING, "offset", "中心索引", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, scale, ORPHEUS_SLOT_SETTING, "scale", "索引缩放", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, max_boost_db, ORPHEUS_SLOT_SETTING, "max_boost_db", "最大提升", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="dB", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, max_cut_db, ORPHEUS_SLOT_SETTING, "max_cut_db", "最大衰减", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="dB", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, ramp_ms, ORPHEUS_SLOT_SETTING, "ramp_ms", "斜坡时间", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="ms", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=0.000000000e+00f, .max_f32=3.000000000e+03f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, ramp_db_per_s, ORPHEUS_SLOT_SETTING, "ramp_db_per_s", "斜坡速率", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="dB/s", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=0.000000000e+00f, .max_f32=5.000000000e+04f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, freeze, ORPHEUS_SLOT_SETTING, "freeze", "冻结斜坡", ORPHEUS_VALUE_BOOL, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, filter_order, ORPHEUS_SLOT_SETTING, "filter_order", "滤波阶数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_i32=1, .max_i32=2)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    int result_boost_coefs=orpheus_builtin_indexed_tone_register_boost_coefs(s, reg);
    if (result_boost_coefs!=ORPHEUS_OK) return result_boost_coefs;
    int result_cut_coefs=orpheus_builtin_indexed_tone_register_cut_coefs(s, reg);
    if (result_cut_coefs!=ORPHEUS_OK) return result_cut_coefs;
    if (ORPHEUS_REG_SLOT(reg, s, execution_mode, ORPHEUS_SLOT_SETTING, "execution_mode", "执行模式（0索引请求，1显式两阶段）", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_i32=0, .max_i32=1)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, phase, ORPHEUS_SLOT_SETTING, "phase", "当前阶段", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_i32=1, .max_i32=2)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, ramp0_current_gain, ORPHEUS_SLOT_SETTING, "ramp0_current_gain", "阶段0当前线性增益", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, ramp0_target_gain, ORPHEUS_SLOT_SETTING, "ramp0_target_gain", "阶段0目标线性增益", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, ramp0_coefficient, ORPHEUS_SLOT_SETTING, "ramp0_coefficient", "阶段0逐样本乘数", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, ramp0_remaining_blocks, ORPHEUS_SLOT_SETTING, "ramp0_remaining_blocks", "阶段0剩余块数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_i32=(-2147483647 - 1), .max_i32=2147483647)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, ramp1_current_gain, ORPHEUS_SLOT_SETTING, "ramp1_current_gain", "阶段1当前线性增益", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, ramp1_target_gain, ORPHEUS_SLOT_SETTING, "ramp1_target_gain", "阶段1目标线性增益", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, ramp1_coefficient, ORPHEUS_SLOT_SETTING, "ramp1_coefficient", "阶段1逐样本乘数", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, ramp1_remaining_blocks, ORPHEUS_SLOT_SETTING, "ramp1_remaining_blocks", "阶段1剩余块数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_i32=(-2147483647 - 1), .max_i32=2147483647)==ORPHEUS_SLOT_ID_INVALID)
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
    orpheus_builtin_indexed_tone_State* s=(orpheus_builtin_indexed_tone_State*)state;
    orpheus_builtin_indexed_tone_IO io={0};
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
    return orpheus_builtin_indexed_tone_algorithm(s, &io, ctx);
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
