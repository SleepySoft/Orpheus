#include "component_state.h"
#include <string.h>
#include <math.h>

_Static_assert(_Generic(&(((orpheus_builtin_biquad_bank_State*)0)->algorithm.stage0.coefs), float (*)[5]: 1, default: 0),
    "BULK bq0_coefs: storage 类型或固定容量不匹配");
_Static_assert(_Generic(&(((orpheus_builtin_biquad_bank_State*)0)->algorithm.stage1.coefs), float (*)[5]: 1, default: 0),
    "BULK bq1_coefs: storage 类型或固定容量不匹配");
static const OrpheusPort ports[] = {
    {.id="in", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
};

static const OrpheusParameter params[] = {
    {.id="fc0", .name="第 1 段频率", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=1.000000000e+03f}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="Hz", .readback=false, .persistent=true, .affects_signature=false, .min_f32=2.000000000e+01f, .max_f32=2.000000000e+04f},
    {.id="q0", .name="第 1 段 Q", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=1.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=false, .persistent=true, .affects_signature=false, .min_f32=1.000000000e-01f, .max_f32=1.000000000e+01f},
    {.id="gain_db0", .name="第 1 段增益", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="dB", .readback=false, .persistent=true, .affects_signature=false, .min_f32=-2.400000000e+01f, .max_f32=2.400000000e+01f},
    {.id="fc1", .name="第 2 段频率", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=3.000000000e+03f}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="Hz", .readback=false, .persistent=true, .affects_signature=false, .min_f32=2.000000000e+01f, .max_f32=2.000000000e+04f},
    {.id="q1", .name="第 2 段 Q", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=1.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=false, .persistent=true, .affects_signature=false, .min_f32=1.000000000e-01f, .max_f32=1.000000000e+01f},
    {.id="gain_db1", .name="第 2 段增益", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="dB", .readback=false, .persistent=true, .affects_signature=false, .min_f32=-2.400000000e+01f, .max_f32=2.400000000e+01f},
    {.id="channels", .name="通道数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=2}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=1, .max_i32=32},
    {.id="form", .name="滤波结构", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str="df2t"}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="bq0_coefs", .name="第 1 段系数", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str=""}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=false, .persistent=false, .affects_signature=false},
    {.id="bq1_coefs", .name="第 2 段系数", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str=""}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=false, .persistent=false, .affects_signature=false},
};

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.biquad_bank", .version="1.1.0", .abi_version=7,
    .ports=ports, .port_count=2,
    .params=params, .param_count=10,
    .state_size=sizeof(orpheus_builtin_biquad_bank_State), .alignment=_Alignof(orpheus_builtin_biquad_bank_State),
    .scratch_size=0, .latency_samples=0,
    .realtime_safe=true, .supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

typedef struct orpheus_builtin_biquad_bank_Parameters {
    float fc0;
    float q0;
    float gain_db0;
    float fc1;
    float q1;
    float gain_db1;
    int32_t channels;
    char form[8];
    char bq0_coefs[256];
    char bq1_coefs[256];
} orpheus_builtin_biquad_bank_Parameters;
static void snapshot_parameters(const orpheus_builtin_biquad_bank_State* s, orpheus_builtin_biquad_bank_Parameters* p) {
    (void)s; memset(p, 0, sizeof(*p));
    p->fc0=s->fc0;
    p->q0=s->q0;
    p->gain_db0=s->gain_db0;
    p->fc1=s->fc1;
    p->q1=s->q1;
    p->gain_db1=s->gain_db1;
    p->channels=s->channels;
    memcpy(p->form, s->form, sizeof(p->form));
    memcpy(p->bq0_coefs, s->bq0_coefs, sizeof(p->bq0_coefs));
    memcpy(p->bq1_coefs, s->bq1_coefs, sizeof(p->bq1_coefs));
}
static void commit_parameters(orpheus_builtin_biquad_bank_State* s, const orpheus_builtin_biquad_bank_Parameters* p) {
    (void)s; (void)p;
    s->fc0=p->fc0;
    s->q0=p->q0;
    s->gain_db0=p->gain_db0;
    s->fc1=p->fc1;
    s->q1=p->q1;
    s->gain_db1=p->gain_db1;
    s->channels=p->channels;
    memcpy(s->form, p->form, sizeof(s->form));
    memcpy(s->bq0_coefs, p->bq0_coefs, sizeof(s->bq0_coefs));
    memcpy(s->bq1_coefs, p->bq1_coefs, sizeof(s->bq1_coefs));
}
static void default_parameters(orpheus_builtin_biquad_bank_Parameters* p) {
    memset(p, 0, sizeof(*p));
    p->fc0=1.000000000e+03f;
    p->q0=1.000000000e+00f;
    p->gain_db0=0.000000000e+00f;
    p->fc1=3.000000000e+03f;
    p->q1=1.000000000e+00f;
    p->gain_db1=0.000000000e+00f;
    p->channels=2;
    memcpy(p->form, "df2t", 5);
    memcpy(p->bq0_coefs, "", 1);
    memcpy(p->bq1_coefs, "", 1);
}
static int resource_count_bq0_coefs(const orpheus_builtin_biquad_bank_Parameters* p, uint32_t* count) {
    (void)p; uint32_t product=1;
    int64_t dimension0=5;
    if (dimension0<1 || (uint64_t)dimension0>5u/product) return ORPHEUS_ERR_INVALID_ARG;
    product*=(uint32_t)dimension0;
    *count=product; return ORPHEUS_OK;
}
static int resource_count_bq1_coefs(const orpheus_builtin_biquad_bank_Parameters* p, uint32_t* count) {
    (void)p; uint32_t product=1;
    int64_t dimension0=5;
    if (dimension0<1 || (uint64_t)dimension0>5u/product) return ORPHEUS_ERR_INVALID_ARG;
    product*=(uint32_t)dimension0;
    *count=product; return ORPHEUS_OK;
}
static int validate_resource_counts(const orpheus_builtin_biquad_bank_Parameters* p) {
    uint32_t count; int result;
    result=resource_count_bq0_coefs(p, &count);
    if (result!=ORPHEUS_OK) return result;
    result=resource_count_bq1_coefs(p, &count);
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
static int update_parameter(orpheus_builtin_biquad_bank_Parameters* p, const char* key, const OrpheusValue* value) {
    if (!p || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "fc0") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<2.000000000e+01f || value->value.f32>2.000000000e+04f) return ORPHEUS_ERR_INVALID_ARG;
        p->fc0=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "q0") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<1.000000000e-01f || value->value.f32>1.000000000e+01f) return ORPHEUS_ERR_INVALID_ARG;
        p->q0=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "gain_db0") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<-2.400000000e+01f || value->value.f32>2.400000000e+01f) return ORPHEUS_ERR_INVALID_ARG;
        p->gain_db0=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "fc1") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<2.000000000e+01f || value->value.f32>2.000000000e+04f) return ORPHEUS_ERR_INVALID_ARG;
        p->fc1=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "q1") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<1.000000000e-01f || value->value.f32>1.000000000e+01f) return ORPHEUS_ERR_INVALID_ARG;
        p->q1=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "gain_db1") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<-2.400000000e+01f || value->value.f32>2.400000000e+01f) return ORPHEUS_ERR_INVALID_ARG;
        p->gain_db1=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "channels") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>32) return ORPHEUS_ERR_INVALID_ARG;
        p->channels=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "form") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->form), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->form)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->form, text, sizeof(text));
        return ORPHEUS_OK;
    }
    if (strcmp(key, "bq0_coefs") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->bq0_coefs), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->bq0_coefs)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->bq0_coefs, text, sizeof(text));
        return ORPHEUS_OK;
    }
    if (strcmp(key, "bq1_coefs") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->bq1_coefs), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->bq1_coefs)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->bq1_coefs, text, sizeof(text));
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int parameter_value(const orpheus_builtin_biquad_bank_Parameters* p, const char* key, OrpheusValue* value) {
    (void)p; (void)key; (void)value;
    if (strcmp(key, "fc0")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->fc0; return ORPHEUS_OK;
    }
    if (strcmp(key, "q0")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->q0; return ORPHEUS_OK;
    }
    if (strcmp(key, "gain_db0")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->gain_db0; return ORPHEUS_OK;
    }
    if (strcmp(key, "fc1")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->fc1; return ORPHEUS_OK;
    }
    if (strcmp(key, "q1")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->q1; return ORPHEUS_OK;
    }
    if (strcmp(key, "gain_db1")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->gain_db1; return ORPHEUS_OK;
    }
    if (strcmp(key, "channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->channels; return ORPHEUS_OK;
    }
    if (strcmp(key, "form")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=p->form; return ORPHEUS_OK;
    }
    if (strcmp(key, "bq0_coefs")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=p->bq0_coefs; return ORPHEUS_OK;
    }
    if (strcmp(key, "bq1_coefs")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=p->bq1_coefs; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int set_parameter(void* state, const char* key, const OrpheusValue* value) {
    orpheus_builtin_biquad_bank_State* s=(orpheus_builtin_biquad_bank_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_biquad_bank_Parameters before, candidate; snapshot_parameters(s, &before); candidate=before;
    int result=update_parameter(&candidate, key, value);
    if (result!=ORPHEUS_OK) return result;
    result=validate_resource_counts(&candidate); if (result!=ORPHEUS_OK) return result;
    OrpheusValue checked; result=parameter_value(&candidate, key, &checked);
    if (result!=ORPHEUS_OK) return result;
    result=orpheus_builtin_biquad_bank_set_parameter(s, key, &checked);
    if (result!=ORPHEUS_OK) { commit_parameters(s, &before); return result; }
    orpheus_builtin_biquad_bank_Parameters after; snapshot_parameters(s, &after);
    result=update_parameter(&after, key, &checked);
    if (result!=ORPHEUS_OK) { commit_parameters(s, &before); return result; }
    candidate=after;
    s->_orpheus_cleanup_pending=true;
    commit_parameters(s, &candidate); return ORPHEUS_OK;
}
static int get_parameter(void* state, const char* key, OrpheusValue* value) {
    orpheus_builtin_biquad_bank_State* s=(orpheus_builtin_biquad_bank_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "fc0")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->fc0; return ORPHEUS_OK;
    }
    if (strcmp(key, "q0")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->q0; return ORPHEUS_OK;
    }
    if (strcmp(key, "gain_db0")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->gain_db0; return ORPHEUS_OK;
    }
    if (strcmp(key, "fc1")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->fc1; return ORPHEUS_OK;
    }
    if (strcmp(key, "q1")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->q1; return ORPHEUS_OK;
    }
    if (strcmp(key, "gain_db1")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->gain_db1; return ORPHEUS_OK;
    }
    if (strcmp(key, "channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->channels; return ORPHEUS_OK;
    }
    if (strcmp(key, "form")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->form; return ORPHEUS_OK;
    }
    if (strcmp(key, "bq0_coefs")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->bq0_coefs; return ORPHEUS_OK;
    }
    if (strcmp(key, "bq1_coefs")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->bq1_coefs; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int configure_parameters(orpheus_builtin_biquad_bank_Parameters* p, const OrpheusConfig* config) {
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
    return validate_resource_counts(p);
}
static int cleanup(orpheus_builtin_biquad_bank_State* s) {
    int result=ORPHEUS_OK;
    if (s->_orpheus_cleanup_pending) {
        s->_orpheus_cleanup_pending=false;
        memset(&s->algorithm, 0, sizeof(s->algorithm));
    }
    return result;
}
static int prepare(void* state, const OrpheusConfig* config) {
    orpheus_builtin_biquad_bank_State* s=(orpheus_builtin_biquad_bank_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_biquad_bank_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    result=cleanup(s); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate);
    s->_orpheus_cleanup_pending=true;
    result=orpheus_builtin_biquad_bank_prepare(s, config);
    if (result!=ORPHEUS_OK) { (void)cleanup(s); return result; }
    return ORPHEUS_OK;
}
static int create(void** state, const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=NULL;
    if (!config || !config->state_block) return ORPHEUS_ERR_INVALID_ARG;
    if ((uintptr_t)config->state_block % descriptor.alignment) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_biquad_bank_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    orpheus_builtin_biquad_bank_State* s=(orpheus_builtin_biquad_bank_State*)config->state_block;
    memset(s, 0, sizeof(*s)); commit_parameters(s, &candidate);
    s->_orpheus_alive=true; *state=s; return ORPHEUS_OK;
}
static int destroy(void* state) {
    orpheus_builtin_biquad_bank_State* s=(orpheus_builtin_biquad_bank_State*)state;
    if (!s) return ORPHEUS_ERR_INVALID_ARG;
    if (!s->_orpheus_alive) return ORPHEUS_OK;
    int result=cleanup(s); s->_orpheus_alive=false; return result;
}
static int reset(void* state) {
    orpheus_builtin_biquad_bank_State* s=(orpheus_builtin_biquad_bank_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_builtin_biquad_bank_reset(s);
}
int orpheus_builtin_biquad_bank_register_bq0_coefs(orpheus_builtin_biquad_bank_State* s, const OrpheusRegistry* reg) {
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_biquad_bank_Parameters current; snapshot_parameters(s, &current);
    uint32_t count_bq0_coefs;
    if (resource_count_bq0_coefs(&current, &count_bq0_coefs)!=ORPHEUS_OK) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_ARRAY(reg, s, algorithm.stage0.coefs, count_bq0_coefs, ORPHEUS_SLOT_BULK, "bq0.coefs", "第 1 段系数", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_DOUBLE_BUFFERED)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    return ORPHEUS_OK;
}
int orpheus_builtin_biquad_bank_register_bq1_coefs(orpheus_builtin_biquad_bank_State* s, const OrpheusRegistry* reg) {
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_biquad_bank_Parameters current; snapshot_parameters(s, &current);
    uint32_t count_bq1_coefs;
    if (resource_count_bq1_coefs(&current, &count_bq1_coefs)!=ORPHEUS_OK) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_ARRAY(reg, s, algorithm.stage1.coefs, count_bq1_coefs, ORPHEUS_SLOT_BULK, "bq1.coefs", "第 2 段系数", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_DOUBLE_BUFFERED)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    return ORPHEUS_OK;
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    orpheus_builtin_biquad_bank_State* s=(orpheus_builtin_biquad_bank_State*)state;
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_SLOT(reg, s, fc0, ORPHEUS_SLOT_SETTING, "fc0", "第 1 段频率", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="Hz", .flags=ORPHEUS_SLOT_PERSISTENT, .min_f32=2.000000000e+01f, .max_f32=2.000000000e+04f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, q0, ORPHEUS_SLOT_SETTING, "q0", "第 1 段 Q", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_PERSISTENT, .min_f32=1.000000000e-01f, .max_f32=1.000000000e+01f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, gain_db0, ORPHEUS_SLOT_SETTING, "gain_db0", "第 1 段增益", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="dB", .flags=ORPHEUS_SLOT_PERSISTENT, .min_f32=-2.400000000e+01f, .max_f32=2.400000000e+01f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, fc1, ORPHEUS_SLOT_SETTING, "fc1", "第 2 段频率", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="Hz", .flags=ORPHEUS_SLOT_PERSISTENT, .min_f32=2.000000000e+01f, .max_f32=2.000000000e+04f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, q1, ORPHEUS_SLOT_SETTING, "q1", "第 2 段 Q", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_PERSISTENT, .min_f32=1.000000000e-01f, .max_f32=1.000000000e+01f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, gain_db1, ORPHEUS_SLOT_SETTING, "gain_db1", "第 2 段增益", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="dB", .flags=ORPHEUS_SLOT_PERSISTENT, .min_f32=-2.400000000e+01f, .max_f32=2.400000000e+01f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=1, .max_i32=32)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, form, ORPHEUS_SLOT_SETTING, "form", "滤波结构", ORPHEUS_VALUE_STRING, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    int result_bq0_coefs=orpheus_builtin_biquad_bank_register_bq0_coefs(s, reg);
    if (result_bq0_coefs!=ORPHEUS_OK) return result_bq0_coefs;
    int result_bq1_coefs=orpheus_builtin_biquad_bank_register_bq1_coefs(s, reg);
    if (result_bq1_coefs!=ORPHEUS_OK) return result_bq1_coefs;
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
    orpheus_builtin_biquad_bank_State* s=(orpheus_builtin_biquad_bank_State*)state;
    orpheus_builtin_biquad_bank_IO io={0};
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
    return orpheus_builtin_biquad_bank_algorithm(s, &io, ctx);
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
