#include "component_state.h"
#include <string.h>
#include <math.h>

static const OrpheusPort ports[] = {
    {.id="x", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
    {.id="d", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
};

static const OrpheusParameter params[] = {
    {.id="channels", .name="通道数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=2}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=1, .max_i32=8},
    {.id="filter_length", .name="滤波器阶数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=128}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_i32=1, .max_i32=1024},
    {.id="step_size", .name="步长", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=5.000000000e-02f}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_f32=0.000000000e+00f, .max_f32=2.000000000e+00f},
    {.id="leakage", .name="泄漏因子", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=1.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_f32=0.000000000e+00f, .max_f32=1.000000000e+00f},
    {.id="eps", .name="正则化", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=1.000000000e-06f}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_f32=1.000000000e-12f, .max_f32=1.000000000e+00f},
    {.id="secondary_gain", .name="次级路径增益", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=1.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_f32=0.000000000e+00f, .max_f32=1.000000000e+01f},
    {.id="secondary_delay", .name="次路延迟(采样点)", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=3.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_f32=0.000000000e+00f, .max_f32=5.120000000e+02f},
    {.id="noise_reduction_db", .name="降噪量(dB)", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="power_d", .name="残差麦克风功率", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="power_e", .name="消除后功率", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="detail", .name="明细", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str="{}"}, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .readback=true, .persistent=true, .affects_signature=false},
};

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.anc_fxlms", .version="1.0.0", .abi_version=7,
    .ports=ports, .port_count=3,
    .params=params, .param_count=11,
    .state_size=sizeof(orpheus_builtin_anc_fxlms_State), .alignment=_Alignof(orpheus_builtin_anc_fxlms_State),
    .scratch_size=0, .latency_samples=0,
    .realtime_safe=true, .supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

typedef struct orpheus_builtin_anc_fxlms_Parameters {
    int32_t channels;
    int32_t filter_length;
    float step_size;
    float leakage;
    float eps;
    float secondary_gain;
    float secondary_delay;
    float noise_reduction_db;
    float power_d;
    float power_e;
    char detail[512];
} orpheus_builtin_anc_fxlms_Parameters;
static void snapshot_parameters(const orpheus_builtin_anc_fxlms_State* s, orpheus_builtin_anc_fxlms_Parameters* p) {
    (void)s; memset(p, 0, sizeof(*p));
    p->channels=s->channels;
    p->filter_length=s->filter_length;
    p->step_size=s->step_size;
    p->leakage=s->leakage;
    p->eps=s->eps;
    p->secondary_gain=s->secondary_gain;
    p->secondary_delay=s->secondary_delay;
    p->noise_reduction_db=s->noise_reduction_db;
    p->power_d=s->power_d;
    p->power_e=s->power_e;
    memcpy(p->detail, s->detail, sizeof(p->detail));
}
static void commit_parameters(orpheus_builtin_anc_fxlms_State* s, const orpheus_builtin_anc_fxlms_Parameters* p) {
    (void)s; (void)p;
    s->channels=p->channels;
    s->filter_length=p->filter_length;
    s->step_size=p->step_size;
    s->leakage=p->leakage;
    s->eps=p->eps;
    s->secondary_gain=p->secondary_gain;
    s->secondary_delay=p->secondary_delay;
    s->noise_reduction_db=p->noise_reduction_db;
    s->power_d=p->power_d;
    s->power_e=p->power_e;
    memcpy(s->detail, p->detail, sizeof(s->detail));
}
static void default_parameters(orpheus_builtin_anc_fxlms_Parameters* p) {
    memset(p, 0, sizeof(*p));
    p->channels=2;
    p->filter_length=128;
    p->step_size=5.000000000e-02f;
    p->leakage=1.000000000e+00f;
    p->eps=1.000000000e-06f;
    p->secondary_gain=1.000000000e+00f;
    p->secondary_delay=3.000000000e+00f;
    p->noise_reduction_db=0.000000000e+00f;
    p->power_d=0.000000000e+00f;
    p->power_e=0.000000000e+00f;
    memcpy(p->detail, "{}", 3);
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
static int update_parameter(orpheus_builtin_anc_fxlms_Parameters* p, const char* key, const OrpheusValue* value) {
    if (!p || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "channels") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>8) return ORPHEUS_ERR_INVALID_ARG;
        p->channels=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "filter_length") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>1024) return ORPHEUS_ERR_INVALID_ARG;
        p->filter_length=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "step_size") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<0.000000000e+00f || value->value.f32>2.000000000e+00f) return ORPHEUS_ERR_INVALID_ARG;
        p->step_size=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "leakage") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<0.000000000e+00f || value->value.f32>1.000000000e+00f) return ORPHEUS_ERR_INVALID_ARG;
        p->leakage=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "eps") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<1.000000000e-12f || value->value.f32>1.000000000e+00f) return ORPHEUS_ERR_INVALID_ARG;
        p->eps=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "secondary_gain") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<0.000000000e+00f || value->value.f32>1.000000000e+01f) return ORPHEUS_ERR_INVALID_ARG;
        p->secondary_gain=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "secondary_delay") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<0.000000000e+00f || value->value.f32>5.120000000e+02f) return ORPHEUS_ERR_INVALID_ARG;
        p->secondary_delay=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "noise_reduction_db") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->noise_reduction_db=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "power_d") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->power_d=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "power_e") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->power_e=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "detail") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->detail), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->detail)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->detail, text, sizeof(text));
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int parameter_value(const orpheus_builtin_anc_fxlms_Parameters* p, const char* key, OrpheusValue* value) {
    (void)p; (void)key; (void)value;
    if (strcmp(key, "channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->channels; return ORPHEUS_OK;
    }
    if (strcmp(key, "filter_length")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->filter_length; return ORPHEUS_OK;
    }
    if (strcmp(key, "step_size")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->step_size; return ORPHEUS_OK;
    }
    if (strcmp(key, "leakage")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->leakage; return ORPHEUS_OK;
    }
    if (strcmp(key, "eps")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->eps; return ORPHEUS_OK;
    }
    if (strcmp(key, "secondary_gain")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->secondary_gain; return ORPHEUS_OK;
    }
    if (strcmp(key, "secondary_delay")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->secondary_delay; return ORPHEUS_OK;
    }
    if (strcmp(key, "noise_reduction_db")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->noise_reduction_db; return ORPHEUS_OK;
    }
    if (strcmp(key, "power_d")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->power_d; return ORPHEUS_OK;
    }
    if (strcmp(key, "power_e")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->power_e; return ORPHEUS_OK;
    }
    if (strcmp(key, "detail")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=p->detail; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int set_parameter(void* state, const char* key, const OrpheusValue* value) {
    orpheus_builtin_anc_fxlms_State* s=(orpheus_builtin_anc_fxlms_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "noise_reduction_db")==0) return ORPHEUS_ERR_UNSUPPORTED;
    if (strcmp(key, "power_d")==0) return ORPHEUS_ERR_UNSUPPORTED;
    if (strcmp(key, "power_e")==0) return ORPHEUS_ERR_UNSUPPORTED;
    if (strcmp(key, "detail")==0) return ORPHEUS_ERR_UNSUPPORTED;
    orpheus_builtin_anc_fxlms_Parameters before, candidate; snapshot_parameters(s, &before); candidate=before;
    int result=update_parameter(&candidate, key, value);
    if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate); return ORPHEUS_OK;
}
static int get_parameter(void* state, const char* key, OrpheusValue* value) {
    orpheus_builtin_anc_fxlms_State* s=(orpheus_builtin_anc_fxlms_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_anc_fxlms_Parameters before, checked; snapshot_parameters(s, &before); checked=before;
    OrpheusValue candidate={0};
    int result=orpheus_builtin_anc_fxlms_get_parameter(s, key, &candidate);
    if (result==ORPHEUS_OK) result=update_parameter(&checked, key, &candidate);
    commit_parameters(s, &before);
    if (result!=ORPHEUS_ERR_NOT_FOUND) {
        if (result!=ORPHEUS_OK) return result;
        result=parameter_value(&checked, key, value);
        if (result!=ORPHEUS_OK) return result;
        if (strcmp(key, "detail")==0) {
            memcpy(s->_orpheus_readback_detail, checked.detail, sizeof(checked.detail));
            value->value.str=s->_orpheus_readback_detail;
        }
        return ORPHEUS_OK;
    }
    if (strcmp(key, "channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->channels; return ORPHEUS_OK;
    }
    if (strcmp(key, "filter_length")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->filter_length; return ORPHEUS_OK;
    }
    if (strcmp(key, "step_size")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->step_size; return ORPHEUS_OK;
    }
    if (strcmp(key, "leakage")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->leakage; return ORPHEUS_OK;
    }
    if (strcmp(key, "eps")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->eps; return ORPHEUS_OK;
    }
    if (strcmp(key, "secondary_gain")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->secondary_gain; return ORPHEUS_OK;
    }
    if (strcmp(key, "secondary_delay")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->secondary_delay; return ORPHEUS_OK;
    }
    if (strcmp(key, "noise_reduction_db")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->noise_reduction_db; return ORPHEUS_OK;
    }
    if (strcmp(key, "power_d")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->power_d; return ORPHEUS_OK;
    }
    if (strcmp(key, "power_e")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->power_e; return ORPHEUS_OK;
    }
    if (strcmp(key, "detail")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->detail; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int configure_parameters(orpheus_builtin_anc_fxlms_Parameters* p, const OrpheusConfig* config) {
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
static int cleanup(orpheus_builtin_anc_fxlms_State* s) {
    int result=ORPHEUS_OK;
    if (s->_orpheus_cleanup_pending) {
        s->_orpheus_cleanup_pending=false;
        result=orpheus_builtin_anc_fxlms_destroy(s);
        memset(&s->algorithm, 0, sizeof(s->algorithm));
    }
    return result;
}
static int prepare(void* state, const OrpheusConfig* config) {
    orpheus_builtin_anc_fxlms_State* s=(orpheus_builtin_anc_fxlms_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_anc_fxlms_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    result=cleanup(s); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate);
    s->_orpheus_cleanup_pending=true;
    result=orpheus_builtin_anc_fxlms_prepare(s, config);
    if (result!=ORPHEUS_OK) { (void)cleanup(s); return result; }
    return ORPHEUS_OK;
}
static int create(void** state, const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=NULL;
    if (!config || !config->state_block) return ORPHEUS_ERR_INVALID_ARG;
    if ((uintptr_t)config->state_block % descriptor.alignment) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_anc_fxlms_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    orpheus_builtin_anc_fxlms_State* s=(orpheus_builtin_anc_fxlms_State*)config->state_block;
    memset(s, 0, sizeof(*s)); commit_parameters(s, &candidate);
    s->_orpheus_alive=true; *state=s; return ORPHEUS_OK;
}
static int destroy(void* state) {
    orpheus_builtin_anc_fxlms_State* s=(orpheus_builtin_anc_fxlms_State*)state;
    if (!s) return ORPHEUS_ERR_INVALID_ARG;
    if (!s->_orpheus_alive) return ORPHEUS_OK;
    int result=cleanup(s); s->_orpheus_alive=false; return result;
}
static int reset(void* state) {
    orpheus_builtin_anc_fxlms_State* s=(orpheus_builtin_anc_fxlms_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_builtin_anc_fxlms_reset(s);
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    orpheus_builtin_anc_fxlms_State* s=(orpheus_builtin_anc_fxlms_State*)state;
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=1, .max_i32=8)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, filter_length, ORPHEUS_SLOT_SETTING, "filter_length", "滤波器阶数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_i32=1, .max_i32=1024)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, step_size, ORPHEUS_SLOT_SETTING, "step_size", "步长", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=0.000000000e+00f, .max_f32=2.000000000e+00f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, leakage, ORPHEUS_SLOT_SETTING, "leakage", "泄漏因子", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=0.000000000e+00f, .max_f32=1.000000000e+00f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, eps, ORPHEUS_SLOT_SETTING, "eps", "正则化", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=1.000000000e-12f, .max_f32=1.000000000e+00f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, secondary_gain, ORPHEUS_SLOT_SETTING, "secondary_gain", "次级路径增益", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=0.000000000e+00f, .max_f32=1.000000000e+01f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, secondary_delay, ORPHEUS_SLOT_SETTING, "secondary_delay", "次路延迟(采样点)", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=0.000000000e+00f, .max_f32=5.120000000e+02f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, noise_reduction_db, ORPHEUS_SLOT_PROBE, "noise_reduction_db", "降噪量(dB)", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, power_d, ORPHEUS_SLOT_PROBE, "power_d", "残差麦克风功率", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, power_e, ORPHEUS_SLOT_PROBE, "power_e", "消除后功率", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, detail, ORPHEUS_SLOT_PROBE, "detail", "明细", ORPHEUS_VALUE_STRING, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
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
    orpheus_builtin_anc_fxlms_State* s=(orpheus_builtin_anc_fxlms_State*)state;
    orpheus_builtin_anc_fxlms_IO io={0};
    if (!s || !s->_orpheus_alive || !ctx) return ORPHEUS_ERR_INVALID_ARG;
    io.x.input_index=0;
    io.x.buffer=(ctx->inputs && ctx->input_count > (0)) ? ctx->inputs[0] : NULL;
    if (!valid_buffer(io.x.buffer, (uint32_t)(s->channels),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.x.samples=io.x.buffer ? (const float*)io.x.buffer->data : NULL;
    io.d.input_index=1;
    io.d.buffer=(ctx->inputs && ctx->input_count > (1)) ? ctx->inputs[1] : NULL;
    if (!valid_buffer(io.d.buffer, (uint32_t)(s->channels),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.d.samples=io.d.buffer ? (const float*)io.d.buffer->data : NULL;
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
    return orpheus_builtin_anc_fxlms_algorithm(s, &io, ctx);
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
