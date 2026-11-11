#include "component_state.h"
#include <string.h>
#include <math.h>

static const OrpheusPort ports[] = {
    {.id="rms", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_CONTROL, .sample_format=ORPHEUS_FORMAT_F32, .channels=1, .sample_rate=0, .block_size=1, .is_variable=false, .channels_param=NULL},
    {.id="peak", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_CONTROL, .sample_format=ORPHEUS_FORMAT_F32, .channels=1, .sample_rate=0, .block_size=1, .is_variable=false, .channels_param=NULL},
    {.id="volume", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_CONTROL, .sample_format=ORPHEUS_FORMAT_F32, .channels=1, .sample_rate=0, .block_size=1, .is_variable=false, .channels_param=NULL},
    {.id="fast_trigger", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_CONTROL, .sample_format=ORPHEUS_FORMAT_F32, .channels=1, .sample_rate=0, .block_size=1, .is_variable=false, .channels_param=NULL},
    {.id="volume_gain", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_CONTROL, .sample_format=ORPHEUS_FORMAT_F32, .channels=1, .sample_rate=0, .block_size=1, .is_variable=false, .channels_param=NULL},
    {.id="level", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_CONTROL, .sample_format=ORPHEUS_FORMAT_F32, .channels=1, .sample_rate=0, .block_size=1, .is_variable=false, .channels_param=NULL},
    {.id="envelope", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_CONTROL, .sample_format=ORPHEUS_FORMAT_F32, .channels=1, .sample_rate=0, .block_size=1, .is_variable=false, .channels_param=NULL},
};

static const OrpheusParameter params[] = {
    {.id="rms_db", .name="无输入时RMS电平", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=-1.000000000e+02f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="peak_db", .name="无输入时峰值电平", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=-1.000000000e+02f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="prediction_db_spl", .name="声压级预测偏置", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=1.000000000e+02f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="slow_decay_db", .name="慢衰减每次增量", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=-1.000000000e-02f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="hold_margin_db", .name="保持触发差值（非负）", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=3.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="hold_calls", .name="保持调用数（非负）", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=1.000000000e+01f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="fast_decay_db", .name="快衰减每次增量", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=-1.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="min_db", .name="包络下限", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=-1.000000000e+02f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="max_db", .name="包络上限", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="volume_db", .name="无输入时音量补偿", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="trigger", .name="无输入时快速触发", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="trigger_threshold", .name="快速触发阈值", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="fast_multiplier", .name="快速触发衰减倍数", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=1.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="mode", .name="模式（负数冻结，0均方，正数峰值）", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="volume_epsilon", .name="线性音量转dB正偏置", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=2.220446050e-16f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="level_db_spl", .name="声压级输出", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .readback=true, .persistent=false, .affects_signature=false},
    {.id="envelope_db", .name="当前包络", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .readback=true, .persistent=false, .affects_signature=false},
    {.id="holding", .name="保持状态", .type=ORPHEUS_VALUE_BOOL, .default_value={.type=ORPHEUS_VALUE_BOOL, .value.b=true}, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .readback=true, .persistent=false, .affects_signature=false},
};

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.level_tracker", .version="1.1.0", .abi_version=7,
    .ports=ports, .port_count=7,
    .params=params, .param_count=18,
    .state_size=sizeof(orpheus_builtin_level_tracker_State), .alignment=_Alignof(orpheus_builtin_level_tracker_State),
    .scratch_size=0, .latency_samples=0,
    .realtime_safe=true, .supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

static void snapshot_parameters(const orpheus_builtin_level_tracker_State* s, orpheus_builtin_level_tracker_Parameters* p) {
    (void)s; memset(p, 0, sizeof(*p));
    p->rms_db=s->rms_db;
    p->peak_db=s->peak_db;
    p->prediction_db_spl=s->prediction_db_spl;
    p->slow_decay_db=s->slow_decay_db;
    p->hold_margin_db=s->hold_margin_db;
    p->hold_calls=s->hold_calls;
    p->fast_decay_db=s->fast_decay_db;
    p->min_db=s->min_db;
    p->max_db=s->max_db;
    p->volume_db=s->volume_db;
    p->trigger=s->trigger;
    p->trigger_threshold=s->trigger_threshold;
    p->fast_multiplier=s->fast_multiplier;
    p->mode=s->mode;
    p->volume_epsilon=s->volume_epsilon;
    p->level_db_spl=s->level_db_spl;
    p->envelope_db=s->envelope_db;
    p->holding=s->holding;
}
static void commit_parameters(orpheus_builtin_level_tracker_State* s, const orpheus_builtin_level_tracker_Parameters* p) {
    (void)s; (void)p;
    s->rms_db=p->rms_db;
    s->peak_db=p->peak_db;
    s->prediction_db_spl=p->prediction_db_spl;
    s->slow_decay_db=p->slow_decay_db;
    s->hold_margin_db=p->hold_margin_db;
    s->hold_calls=p->hold_calls;
    s->fast_decay_db=p->fast_decay_db;
    s->min_db=p->min_db;
    s->max_db=p->max_db;
    s->volume_db=p->volume_db;
    s->trigger=p->trigger;
    s->trigger_threshold=p->trigger_threshold;
    s->fast_multiplier=p->fast_multiplier;
    s->mode=p->mode;
    s->volume_epsilon=p->volume_epsilon;
    s->level_db_spl=p->level_db_spl;
    s->envelope_db=p->envelope_db;
    s->holding=p->holding;
}
static void default_parameters(orpheus_builtin_level_tracker_Parameters* p) {
    memset(p, 0, sizeof(*p));
    p->rms_db=-1.000000000e+02f;
    p->peak_db=-1.000000000e+02f;
    p->prediction_db_spl=1.000000000e+02f;
    p->slow_decay_db=-1.000000000e-02f;
    p->hold_margin_db=3.000000000e+00f;
    p->hold_calls=1.000000000e+01f;
    p->fast_decay_db=-1.000000000e+00f;
    p->min_db=-1.000000000e+02f;
    p->max_db=0.000000000e+00f;
    p->volume_db=0.000000000e+00f;
    p->trigger=0.000000000e+00f;
    p->trigger_threshold=0.000000000e+00f;
    p->fast_multiplier=1.000000000e+00f;
    p->mode=0.000000000e+00f;
    p->volume_epsilon=2.220446050e-16f;
    p->level_db_spl=0.000000000e+00f;
    p->envelope_db=0.000000000e+00f;
    p->holding=true;
}
static int update_parameter(orpheus_builtin_level_tracker_Parameters* p, const char* key, const OrpheusValue* value) {
    if (!p || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "rms_db") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->rms_db=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "peak_db") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->peak_db=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "prediction_db_spl") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->prediction_db_spl=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "slow_decay_db") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->slow_decay_db=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "hold_margin_db") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->hold_margin_db=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "hold_calls") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->hold_calls=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "fast_decay_db") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->fast_decay_db=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "min_db") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->min_db=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "max_db") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->max_db=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "volume_db") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->volume_db=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "trigger") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->trigger=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "trigger_threshold") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->trigger_threshold=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "fast_multiplier") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->fast_multiplier=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "mode") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->mode=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "volume_epsilon") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->volume_epsilon=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "level_db_spl") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->level_db_spl=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "envelope_db") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->envelope_db=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "holding") == 0) {
        if (value->type!=ORPHEUS_VALUE_BOOL) return ORPHEUS_ERR_INVALID_ARG;
        p->holding=value->value.b;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int set_parameter(void* state, const char* key, const OrpheusValue* value) {
    orpheus_builtin_level_tracker_State* s=(orpheus_builtin_level_tracker_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "level_db_spl")==0) return ORPHEUS_ERR_UNSUPPORTED;
    if (strcmp(key, "envelope_db")==0) return ORPHEUS_ERR_UNSUPPORTED;
    if (strcmp(key, "holding")==0) return ORPHEUS_ERR_UNSUPPORTED;
    orpheus_builtin_level_tracker_Parameters before, candidate; snapshot_parameters(s, &before); candidate=before;
    int result=update_parameter(&candidate, key, value);
    if (result!=ORPHEUS_OK) return result;
    result=orpheus_builtin_level_tracker_validate(&candidate); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate); return ORPHEUS_OK;
}
static int get_parameter(void* state, const char* key, OrpheusValue* value) {
    orpheus_builtin_level_tracker_State* s=(orpheus_builtin_level_tracker_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "rms_db")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->rms_db; return ORPHEUS_OK;
    }
    if (strcmp(key, "peak_db")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->peak_db; return ORPHEUS_OK;
    }
    if (strcmp(key, "prediction_db_spl")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->prediction_db_spl; return ORPHEUS_OK;
    }
    if (strcmp(key, "slow_decay_db")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->slow_decay_db; return ORPHEUS_OK;
    }
    if (strcmp(key, "hold_margin_db")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->hold_margin_db; return ORPHEUS_OK;
    }
    if (strcmp(key, "hold_calls")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->hold_calls; return ORPHEUS_OK;
    }
    if (strcmp(key, "fast_decay_db")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->fast_decay_db; return ORPHEUS_OK;
    }
    if (strcmp(key, "min_db")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->min_db; return ORPHEUS_OK;
    }
    if (strcmp(key, "max_db")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->max_db; return ORPHEUS_OK;
    }
    if (strcmp(key, "volume_db")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->volume_db; return ORPHEUS_OK;
    }
    if (strcmp(key, "trigger")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->trigger; return ORPHEUS_OK;
    }
    if (strcmp(key, "trigger_threshold")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->trigger_threshold; return ORPHEUS_OK;
    }
    if (strcmp(key, "fast_multiplier")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->fast_multiplier; return ORPHEUS_OK;
    }
    if (strcmp(key, "mode")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->mode; return ORPHEUS_OK;
    }
    if (strcmp(key, "volume_epsilon")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->volume_epsilon; return ORPHEUS_OK;
    }
    if (strcmp(key, "level_db_spl")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->level_db_spl; return ORPHEUS_OK;
    }
    if (strcmp(key, "envelope_db")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->envelope_db; return ORPHEUS_OK;
    }
    if (strcmp(key, "holding")==0) {
        value->type=ORPHEUS_VALUE_BOOL; value->value.b=s->holding; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int configure_parameters(orpheus_builtin_level_tracker_Parameters* p, const OrpheusConfig* config) {
    if (!config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    default_parameters(p);
    for (uint32_t i=0; i<config->param_count; ++i) {
        int result=update_parameter(p, config->param_ids[i], &config->param_values[i]);
        if (result!=ORPHEUS_OK) return result;
    }
    return orpheus_builtin_level_tracker_validate(p);
}
static int cleanup(orpheus_builtin_level_tracker_State* s) {
    int result=ORPHEUS_OK;
    if (s->_orpheus_cleanup_pending) {
        s->_orpheus_cleanup_pending=false;
        memset(&s->algorithm, 0, sizeof(s->algorithm));
    }
    return result;
}
static int prepare(void* state, const OrpheusConfig* config) {
    orpheus_builtin_level_tracker_State* s=(orpheus_builtin_level_tracker_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_level_tracker_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    result=cleanup(s); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate);
    s->_orpheus_cleanup_pending=true;
    result=orpheus_builtin_level_tracker_prepare(s, config);
    if (result!=ORPHEUS_OK) { (void)cleanup(s); return result; }
    return ORPHEUS_OK;
}
static int create(void** state, const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=NULL;
    if (!config || !config->state_block) return ORPHEUS_ERR_INVALID_ARG;
    if ((uintptr_t)config->state_block % descriptor.alignment) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_level_tracker_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    orpheus_builtin_level_tracker_State* s=(orpheus_builtin_level_tracker_State*)config->state_block;
    memset(s, 0, sizeof(*s)); commit_parameters(s, &candidate);
    s->_orpheus_alive=true; *state=s; return ORPHEUS_OK;
}
static int destroy(void* state) {
    orpheus_builtin_level_tracker_State* s=(orpheus_builtin_level_tracker_State*)state;
    if (!s) return ORPHEUS_ERR_INVALID_ARG;
    if (!s->_orpheus_alive) return ORPHEUS_OK;
    int result=cleanup(s); s->_orpheus_alive=false; return result;
}
static int reset(void* state) {
    orpheus_builtin_level_tracker_State* s=(orpheus_builtin_level_tracker_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_builtin_level_tracker_reset(s);
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    orpheus_builtin_level_tracker_State* s=(orpheus_builtin_level_tracker_State*)state;
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_SLOT(reg, s, rms_db, ORPHEUS_SLOT_SETTING, "rms_db", "无输入时RMS电平", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, peak_db, ORPHEUS_SLOT_SETTING, "peak_db", "无输入时峰值电平", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, prediction_db_spl, ORPHEUS_SLOT_SETTING, "prediction_db_spl", "声压级预测偏置", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, slow_decay_db, ORPHEUS_SLOT_SETTING, "slow_decay_db", "慢衰减每次增量", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, hold_margin_db, ORPHEUS_SLOT_SETTING, "hold_margin_db", "保持触发差值（非负）", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, hold_calls, ORPHEUS_SLOT_SETTING, "hold_calls", "保持调用数（非负）", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, fast_decay_db, ORPHEUS_SLOT_SETTING, "fast_decay_db", "快衰减每次增量", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, min_db, ORPHEUS_SLOT_SETTING, "min_db", "包络下限", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, max_db, ORPHEUS_SLOT_SETTING, "max_db", "包络上限", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, volume_db, ORPHEUS_SLOT_SETTING, "volume_db", "无输入时音量补偿", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, trigger, ORPHEUS_SLOT_SETTING, "trigger", "无输入时快速触发", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, trigger_threshold, ORPHEUS_SLOT_SETTING, "trigger_threshold", "快速触发阈值", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, fast_multiplier, ORPHEUS_SLOT_SETTING, "fast_multiplier", "快速触发衰减倍数", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, mode, ORPHEUS_SLOT_SETTING, "mode", "模式（负数冻结，0均方，正数峰值）", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, volume_epsilon, ORPHEUS_SLOT_SETTING, "volume_epsilon", "线性音量转dB正偏置", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, level_db_spl, ORPHEUS_SLOT_PROBE, "level_db_spl", "声压级输出", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .flags=ORPHEUS_SLOT_READBACK)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, envelope_db, ORPHEUS_SLOT_PROBE, "envelope_db", "当前包络", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .flags=ORPHEUS_SLOT_READBACK)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, holding, ORPHEUS_SLOT_PROBE, "holding", "保持状态", ORPHEUS_VALUE_BOOL, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .flags=ORPHEUS_SLOT_READBACK)==ORPHEUS_SLOT_ID_INVALID)
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
    orpheus_builtin_level_tracker_State* s=(orpheus_builtin_level_tracker_State*)state;
    orpheus_builtin_level_tracker_IO io={0};
    if (!s || !s->_orpheus_alive || !ctx) return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_payload_rms=(double)(1);
    if (!isfinite(_orpheus_payload_rms) || _orpheus_payload_rms < 1
            || _orpheus_payload_rms > UINT32_MAX || _orpheus_payload_rms != (uint32_t)_orpheus_payload_rms)
        return ORPHEUS_ERR_INVALID_ARG;
    io.rms.input_index=0;
    io.rms.buffer=(ctx->inputs && ctx->input_count > (0)) ? ctx->inputs[0] : NULL;
    if (!valid_buffer(io.rms.buffer, (uint32_t)(1),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.rms.samples=io.rms.buffer ? (const float*)io.rms.buffer->data : NULL;
    if (io.rms.buffer && io.rms.buffer->frame_count != (uint32_t)_orpheus_payload_rms)
        return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_payload_peak=(double)(1);
    if (!isfinite(_orpheus_payload_peak) || _orpheus_payload_peak < 1
            || _orpheus_payload_peak > UINT32_MAX || _orpheus_payload_peak != (uint32_t)_orpheus_payload_peak)
        return ORPHEUS_ERR_INVALID_ARG;
    io.peak.input_index=1;
    io.peak.buffer=(ctx->inputs && ctx->input_count > (1)) ? ctx->inputs[1] : NULL;
    if (!valid_buffer(io.peak.buffer, (uint32_t)(1),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.peak.samples=io.peak.buffer ? (const float*)io.peak.buffer->data : NULL;
    if (io.peak.buffer && io.peak.buffer->frame_count != (uint32_t)_orpheus_payload_peak)
        return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_payload_volume=(double)(1);
    if (!isfinite(_orpheus_payload_volume) || _orpheus_payload_volume < 1
            || _orpheus_payload_volume > UINT32_MAX || _orpheus_payload_volume != (uint32_t)_orpheus_payload_volume)
        return ORPHEUS_ERR_INVALID_ARG;
    io.volume.input_index=2;
    io.volume.buffer=(ctx->inputs && ctx->input_count > (2)) ? ctx->inputs[2] : NULL;
    if (!valid_buffer(io.volume.buffer, (uint32_t)(1),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.volume.samples=io.volume.buffer ? (const float*)io.volume.buffer->data : NULL;
    if (io.volume.buffer && io.volume.buffer->frame_count != (uint32_t)_orpheus_payload_volume)
        return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_payload_fast_trigger=(double)(1);
    if (!isfinite(_orpheus_payload_fast_trigger) || _orpheus_payload_fast_trigger < 1
            || _orpheus_payload_fast_trigger > UINT32_MAX || _orpheus_payload_fast_trigger != (uint32_t)_orpheus_payload_fast_trigger)
        return ORPHEUS_ERR_INVALID_ARG;
    io.fast_trigger.input_index=3;
    io.fast_trigger.buffer=(ctx->inputs && ctx->input_count > (3)) ? ctx->inputs[3] : NULL;
    if (!valid_buffer(io.fast_trigger.buffer, (uint32_t)(1),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.fast_trigger.samples=io.fast_trigger.buffer ? (const float*)io.fast_trigger.buffer->data : NULL;
    if (io.fast_trigger.buffer && io.fast_trigger.buffer->frame_count != (uint32_t)_orpheus_payload_fast_trigger)
        return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_payload_volume_gain=(double)(1);
    if (!isfinite(_orpheus_payload_volume_gain) || _orpheus_payload_volume_gain < 1
            || _orpheus_payload_volume_gain > UINT32_MAX || _orpheus_payload_volume_gain != (uint32_t)_orpheus_payload_volume_gain)
        return ORPHEUS_ERR_INVALID_ARG;
    io.volume_gain.input_index=4;
    io.volume_gain.buffer=(ctx->inputs && ctx->input_count > (4)) ? ctx->inputs[4] : NULL;
    if (!valid_buffer(io.volume_gain.buffer, (uint32_t)(1),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.volume_gain.samples=io.volume_gain.buffer ? (const float*)io.volume_gain.buffer->data : NULL;
    if (io.volume_gain.buffer && io.volume_gain.buffer->frame_count != (uint32_t)_orpheus_payload_volume_gain)
        return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_payload_level=(double)(1);
    if (!isfinite(_orpheus_payload_level) || _orpheus_payload_level < 1
            || _orpheus_payload_level > UINT32_MAX || _orpheus_payload_level != (uint32_t)_orpheus_payload_level)
        return ORPHEUS_ERR_INVALID_ARG;
    io.level.active=true;
    io.level.drive_port_no=1u;
    io.level.buffer=(ctx->outputs && ctx->output_count > (0)) ? ctx->outputs[0] : NULL;
    if (!valid_buffer(io.level.buffer, (uint32_t)(1),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.level.samples=io.level.buffer ? (float*)io.level.buffer->data : NULL;
    if (io.level.buffer) {
        if ((uint32_t)_orpheus_payload_level > io.level.buffer->frame_capacity) return ORPHEUS_ERR_INVALID_ARG;
        io.level.buffer->frame_count=(uint32_t)_orpheus_payload_level;
    }
    io.level.delivery=(OrpheusDelivery){.buffer=io.level.buffer, .frame_index=ctx->frame_index,
        .epoch=ctx->epoch, .timeline_flags=ctx->timeline_flags};
    double _orpheus_payload_envelope=(double)(1);
    if (!isfinite(_orpheus_payload_envelope) || _orpheus_payload_envelope < 1
            || _orpheus_payload_envelope > UINT32_MAX || _orpheus_payload_envelope != (uint32_t)_orpheus_payload_envelope)
        return ORPHEUS_ERR_INVALID_ARG;
    io.envelope.active=true;
    io.envelope.drive_port_no=2u;
    io.envelope.buffer=(ctx->outputs && ctx->output_count > (1)) ? ctx->outputs[1] : NULL;
    if (!valid_buffer(io.envelope.buffer, (uint32_t)(1),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.envelope.samples=io.envelope.buffer ? (float*)io.envelope.buffer->data : NULL;
    if (io.envelope.buffer) {
        if ((uint32_t)_orpheus_payload_envelope > io.envelope.buffer->frame_capacity) return ORPHEUS_ERR_INVALID_ARG;
        io.envelope.buffer->frame_count=(uint32_t)_orpheus_payload_envelope;
    }
    io.envelope.delivery=(OrpheusDelivery){.buffer=io.envelope.buffer, .frame_index=ctx->frame_index,
        .epoch=ctx->epoch, .timeline_flags=ctx->timeline_flags};
    return orpheus_builtin_level_tracker_algorithm(s, &io, ctx);
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
