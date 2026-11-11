#include "component_state.h"
#include <string.h>
#include <math.h>

static const OrpheusPort ports[] = {
    {.id="in", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
};

static const OrpheusParameter params[] = {
    {.id="level", .name="输入线性 RMS", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_f32=0.000000000e+00f, .max_f32=1.600000000e+01f},
    {.id="target_db", .name="目标 RMS 电平", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=-2.000000000e+01f}, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="dBFS", .readback=true, .persistent=true, .affects_signature=false, .min_f32=-3.600000000e+01f, .max_f32=-6.000000000e+00f},
    {.id="min_gain_db", .name="最大衰减", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=-1.200000000e+01f}, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="dB", .readback=true, .persistent=true, .affects_signature=false, .min_f32=-3.600000000e+01f, .max_f32=0.000000000e+00f},
    {.id="max_gain_db", .name="最大提升", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=1.200000000e+01f}, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="dB", .readback=true, .persistent=true, .affects_signature=false, .min_f32=0.000000000e+00f, .max_f32=2.400000000e+01f},
    {.id="gate_db", .name="静音门", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=-5.500000000e+01f}, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="dBFS", .readback=true, .persistent=true, .affects_signature=false, .min_f32=-9.600000000e+01f, .max_f32=-2.400000000e+01f},
    {.id="detector_attack_ms", .name="检测启动时间", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=8.000000000e+01f}, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="ms", .readback=true, .persistent=true, .affects_signature=false, .min_f32=5.000000000e+00f, .max_f32=1.000000000e+03f},
    {.id="detector_release_ms", .name="检测释放时间", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=6.000000000e+02f}, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="ms", .readback=true, .persistent=true, .affects_signature=false, .min_f32=2.000000000e+01f, .max_f32=5.000000000e+03f},
    {.id="gain_attack_ms", .name="压低响应时间", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=8.000000000e+01f}, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="ms", .readback=true, .persistent=true, .affects_signature=false, .min_f32=5.000000000e+00f, .max_f32=1.000000000e+03f},
    {.id="gain_release_ms", .name="抬升响应时间", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=1.200000000e+03f}, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="ms", .readback=true, .persistent=true, .affects_signature=false, .min_f32=5.000000000e+01f, .max_f32=1.000000000e+04f},
    {.id="channels", .name="通道数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=2}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=1, .max_i32=32},
    {.id="input_db", .name="平滑输入电平", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=-1.200000000e+02f}, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="dBFS", .readback=true, .persistent=false, .affects_signature=false},
    {.id="gain_db", .name="当前目标增益", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="dB", .readback=true, .persistent=false, .affects_signature=false},
    {.id="output_db", .name="估算输出电平", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=-1.200000000e+02f}, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="dBFS", .readback=true, .persistent=false, .affects_signature=false},
};

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.loudness_gain_control", .version="1.0.0", .abi_version=7,
    .ports=ports, .port_count=2,
    .params=params, .param_count=13,
    .state_size=sizeof(orpheus_builtin_loudness_gain_control_State), .alignment=_Alignof(orpheus_builtin_loudness_gain_control_State),
    .scratch_size=0, .latency_samples=0,
    .realtime_safe=true, .supports_inplace=true
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

typedef struct orpheus_builtin_loudness_gain_control_Parameters {
    float level;
    float target_db;
    float min_gain_db;
    float max_gain_db;
    float gate_db;
    float detector_attack_ms;
    float detector_release_ms;
    float gain_attack_ms;
    float gain_release_ms;
    int32_t channels;
    float input_db;
    float gain_db;
    float output_db;
} orpheus_builtin_loudness_gain_control_Parameters;
static void snapshot_parameters(const orpheus_builtin_loudness_gain_control_State* s, orpheus_builtin_loudness_gain_control_Parameters* p) {
    (void)s; memset(p, 0, sizeof(*p));
    p->level=s->level;
    p->target_db=s->target_db;
    p->min_gain_db=s->min_gain_db;
    p->max_gain_db=s->max_gain_db;
    p->gate_db=s->gate_db;
    p->detector_attack_ms=s->detector_attack_ms;
    p->detector_release_ms=s->detector_release_ms;
    p->gain_attack_ms=s->gain_attack_ms;
    p->gain_release_ms=s->gain_release_ms;
    p->channels=s->channels;
    p->input_db=s->input_db;
    p->gain_db=s->gain_db;
    p->output_db=s->output_db;
}
static void commit_parameters(orpheus_builtin_loudness_gain_control_State* s, const orpheus_builtin_loudness_gain_control_Parameters* p) {
    (void)s; (void)p;
    s->level=p->level;
    s->target_db=p->target_db;
    s->min_gain_db=p->min_gain_db;
    s->max_gain_db=p->max_gain_db;
    s->gate_db=p->gate_db;
    s->detector_attack_ms=p->detector_attack_ms;
    s->detector_release_ms=p->detector_release_ms;
    s->gain_attack_ms=p->gain_attack_ms;
    s->gain_release_ms=p->gain_release_ms;
    s->channels=p->channels;
    s->input_db=p->input_db;
    s->gain_db=p->gain_db;
    s->output_db=p->output_db;
}
static void default_parameters(orpheus_builtin_loudness_gain_control_Parameters* p) {
    memset(p, 0, sizeof(*p));
    p->level=0.000000000e+00f;
    p->target_db=-2.000000000e+01f;
    p->min_gain_db=-1.200000000e+01f;
    p->max_gain_db=1.200000000e+01f;
    p->gate_db=-5.500000000e+01f;
    p->detector_attack_ms=8.000000000e+01f;
    p->detector_release_ms=6.000000000e+02f;
    p->gain_attack_ms=8.000000000e+01f;
    p->gain_release_ms=1.200000000e+03f;
    p->channels=2;
    p->input_db=-1.200000000e+02f;
    p->gain_db=0.000000000e+00f;
    p->output_db=-1.200000000e+02f;
}
static int update_parameter(orpheus_builtin_loudness_gain_control_Parameters* p, const char* key, const OrpheusValue* value) {
    if (!p || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "level") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<0.000000000e+00f || value->value.f32>1.600000000e+01f) return ORPHEUS_ERR_INVALID_ARG;
        p->level=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "target_db") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<-3.600000000e+01f || value->value.f32>-6.000000000e+00f) return ORPHEUS_ERR_INVALID_ARG;
        p->target_db=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "min_gain_db") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<-3.600000000e+01f || value->value.f32>0.000000000e+00f) return ORPHEUS_ERR_INVALID_ARG;
        p->min_gain_db=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "max_gain_db") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<0.000000000e+00f || value->value.f32>2.400000000e+01f) return ORPHEUS_ERR_INVALID_ARG;
        p->max_gain_db=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "gate_db") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<-9.600000000e+01f || value->value.f32>-2.400000000e+01f) return ORPHEUS_ERR_INVALID_ARG;
        p->gate_db=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "detector_attack_ms") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<5.000000000e+00f || value->value.f32>1.000000000e+03f) return ORPHEUS_ERR_INVALID_ARG;
        p->detector_attack_ms=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "detector_release_ms") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<2.000000000e+01f || value->value.f32>5.000000000e+03f) return ORPHEUS_ERR_INVALID_ARG;
        p->detector_release_ms=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "gain_attack_ms") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<5.000000000e+00f || value->value.f32>1.000000000e+03f) return ORPHEUS_ERR_INVALID_ARG;
        p->gain_attack_ms=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "gain_release_ms") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<5.000000000e+01f || value->value.f32>1.000000000e+04f) return ORPHEUS_ERR_INVALID_ARG;
        p->gain_release_ms=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "channels") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>32) return ORPHEUS_ERR_INVALID_ARG;
        p->channels=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "input_db") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->input_db=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "gain_db") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->gain_db=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "output_db") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->output_db=value->value.f32;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int set_parameter(void* state, const char* key, const OrpheusValue* value) {
    orpheus_builtin_loudness_gain_control_State* s=(orpheus_builtin_loudness_gain_control_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "input_db")==0) return ORPHEUS_ERR_UNSUPPORTED;
    if (strcmp(key, "gain_db")==0) return ORPHEUS_ERR_UNSUPPORTED;
    if (strcmp(key, "output_db")==0) return ORPHEUS_ERR_UNSUPPORTED;
    orpheus_builtin_loudness_gain_control_Parameters before, candidate; snapshot_parameters(s, &before); candidate=before;
    int result=update_parameter(&candidate, key, value);
    if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate); return ORPHEUS_OK;
}
static int get_parameter(void* state, const char* key, OrpheusValue* value) {
    orpheus_builtin_loudness_gain_control_State* s=(orpheus_builtin_loudness_gain_control_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "level")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->level; return ORPHEUS_OK;
    }
    if (strcmp(key, "target_db")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->target_db; return ORPHEUS_OK;
    }
    if (strcmp(key, "min_gain_db")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->min_gain_db; return ORPHEUS_OK;
    }
    if (strcmp(key, "max_gain_db")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->max_gain_db; return ORPHEUS_OK;
    }
    if (strcmp(key, "gate_db")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->gate_db; return ORPHEUS_OK;
    }
    if (strcmp(key, "detector_attack_ms")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->detector_attack_ms; return ORPHEUS_OK;
    }
    if (strcmp(key, "detector_release_ms")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->detector_release_ms; return ORPHEUS_OK;
    }
    if (strcmp(key, "gain_attack_ms")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->gain_attack_ms; return ORPHEUS_OK;
    }
    if (strcmp(key, "gain_release_ms")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->gain_release_ms; return ORPHEUS_OK;
    }
    if (strcmp(key, "channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->channels; return ORPHEUS_OK;
    }
    if (strcmp(key, "input_db")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->input_db; return ORPHEUS_OK;
    }
    if (strcmp(key, "gain_db")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->gain_db; return ORPHEUS_OK;
    }
    if (strcmp(key, "output_db")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->output_db; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int configure_parameters(orpheus_builtin_loudness_gain_control_Parameters* p, const OrpheusConfig* config) {
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
static int cleanup(orpheus_builtin_loudness_gain_control_State* s) {
    int result=ORPHEUS_OK;
    if (s->_orpheus_cleanup_pending) {
        s->_orpheus_cleanup_pending=false;
        memset(&s->algorithm, 0, sizeof(s->algorithm));
    }
    return result;
}
static int prepare(void* state, const OrpheusConfig* config) {
    orpheus_builtin_loudness_gain_control_State* s=(orpheus_builtin_loudness_gain_control_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_loudness_gain_control_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    result=cleanup(s); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate);
    s->_orpheus_cleanup_pending=true;
    result=orpheus_builtin_loudness_gain_control_prepare(s, config);
    if (result!=ORPHEUS_OK) { (void)cleanup(s); return result; }
    return ORPHEUS_OK;
}
static int create(void** state, const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=NULL;
    if (!config || !config->state_block) return ORPHEUS_ERR_INVALID_ARG;
    if ((uintptr_t)config->state_block % descriptor.alignment) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_loudness_gain_control_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    orpheus_builtin_loudness_gain_control_State* s=(orpheus_builtin_loudness_gain_control_State*)config->state_block;
    memset(s, 0, sizeof(*s)); commit_parameters(s, &candidate);
    s->_orpheus_alive=true; *state=s; return ORPHEUS_OK;
}
static int destroy(void* state) {
    orpheus_builtin_loudness_gain_control_State* s=(orpheus_builtin_loudness_gain_control_State*)state;
    if (!s) return ORPHEUS_ERR_INVALID_ARG;
    if (!s->_orpheus_alive) return ORPHEUS_OK;
    int result=cleanup(s); s->_orpheus_alive=false; return result;
}
static int reset(void* state) {
    orpheus_builtin_loudness_gain_control_State* s=(orpheus_builtin_loudness_gain_control_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_builtin_loudness_gain_control_reset(s);
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    orpheus_builtin_loudness_gain_control_State* s=(orpheus_builtin_loudness_gain_control_State*)state;
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_SLOT(reg, s, level, ORPHEUS_SLOT_SETTING, "level", "输入线性 RMS", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=0.000000000e+00f, .max_f32=1.600000000e+01f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, target_db, ORPHEUS_SLOT_SETTING, "target_db", "目标 RMS 电平", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="dBFS", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=-3.600000000e+01f, .max_f32=-6.000000000e+00f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, min_gain_db, ORPHEUS_SLOT_SETTING, "min_gain_db", "最大衰减", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="dB", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=-3.600000000e+01f, .max_f32=0.000000000e+00f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, max_gain_db, ORPHEUS_SLOT_SETTING, "max_gain_db", "最大提升", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="dB", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=0.000000000e+00f, .max_f32=2.400000000e+01f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, gate_db, ORPHEUS_SLOT_SETTING, "gate_db", "静音门", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="dBFS", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=-9.600000000e+01f, .max_f32=-2.400000000e+01f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, detector_attack_ms, ORPHEUS_SLOT_SETTING, "detector_attack_ms", "检测启动时间", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="ms", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=5.000000000e+00f, .max_f32=1.000000000e+03f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, detector_release_ms, ORPHEUS_SLOT_SETTING, "detector_release_ms", "检测释放时间", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="ms", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=2.000000000e+01f, .max_f32=5.000000000e+03f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, gain_attack_ms, ORPHEUS_SLOT_SETTING, "gain_attack_ms", "压低响应时间", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="ms", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=5.000000000e+00f, .max_f32=1.000000000e+03f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, gain_release_ms, ORPHEUS_SLOT_SETTING, "gain_release_ms", "抬升响应时间", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="ms", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=5.000000000e+01f, .max_f32=1.000000000e+04f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=1, .max_i32=32)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, input_db, ORPHEUS_SLOT_PROBE, "input_db", "平滑输入电平", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="dBFS", .flags=ORPHEUS_SLOT_READBACK)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, gain_db, ORPHEUS_SLOT_PROBE, "gain_db", "当前目标增益", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="dB", .flags=ORPHEUS_SLOT_READBACK)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, output_db, ORPHEUS_SLOT_PROBE, "output_db", "估算输出电平", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="dBFS", .flags=ORPHEUS_SLOT_READBACK)==ORPHEUS_SLOT_ID_INVALID)
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
    orpheus_builtin_loudness_gain_control_State* s=(orpheus_builtin_loudness_gain_control_State*)state;
    orpheus_builtin_loudness_gain_control_IO io={0};
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
    return orpheus_builtin_loudness_gain_control_algorithm(s, &io, ctx);
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
