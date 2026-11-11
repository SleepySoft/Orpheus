#include "component_state.h"
#include <string.h>
#include <math.h>

static const OrpheusPort ports[] = {
    {.id="in", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
    {.id="rms", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_CONTROL, .sample_format=ORPHEUS_FORMAT_F32, .channels=1, .sample_rate=0, .block_size=1, .is_variable=true, .channels_param=NULL},
    {.id="peak", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_CONTROL, .sample_format=ORPHEUS_FORMAT_F32, .channels=1, .sample_rate=0, .block_size=1, .is_variable=true, .channels_param=NULL},
};

static const OrpheusParameter params[] = {
    {.id="mode", .name="模式", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=0}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_i32=0, .max_i32=1},
    {.id="attack_ms", .name="启动时间", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=1.000000000e+01f}, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="ms", .readback=true, .persistent=true, .affects_signature=false, .min_f32=1.000000000e-01f, .max_f32=1.000000000e+03f},
    {.id="release_ms", .name="释放时间", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=1.000000000e+02f}, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="ms", .readback=true, .persistent=true, .affects_signature=false, .min_f32=1.000000000e+00f, .max_f32=5.000000000e+03f},
    {.id="channels", .name="通道数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=2}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=1, .max_i32=32},
    {.id="level", .name="电平", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .readback=true, .persistent=false, .affects_signature=false},
    {.id="execution_mode", .name="执行模式（0平滑包络，1块级双dB统计）", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=0}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=0, .max_i32=1},
    {.id="db_epsilon", .name="dB统计正偏置", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=2.220446050e-16f}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="rms_db", .name="最大通道均方dB", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .readback=true, .persistent=false, .affects_signature=false},
    {.id="peak_db", .name="全块峰值dB", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .readback=true, .persistent=false, .affects_signature=false},
};

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.level_detect", .version="1.1.0", .abi_version=7,
    .ports=ports, .port_count=4,
    .params=params, .param_count=9,
    .state_size=sizeof(orpheus_builtin_level_detect_State), .alignment=_Alignof(orpheus_builtin_level_detect_State),
    .scratch_size=0, .latency_samples=0,
    .realtime_safe=true, .supports_inplace=true
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

static void snapshot_parameters(const orpheus_builtin_level_detect_State* s, orpheus_builtin_level_detect_Parameters* p) {
    (void)s; memset(p, 0, sizeof(*p));
    p->mode=s->mode;
    p->attack_ms=s->attack_ms;
    p->release_ms=s->release_ms;
    p->channels=s->channels;
    p->level=s->level;
    p->execution_mode=s->execution_mode;
    p->db_epsilon=s->db_epsilon;
    p->rms_db=s->rms_db;
    p->peak_db=s->peak_db;
}
static void commit_parameters(orpheus_builtin_level_detect_State* s, const orpheus_builtin_level_detect_Parameters* p) {
    (void)s; (void)p;
    s->mode=p->mode;
    s->attack_ms=p->attack_ms;
    s->release_ms=p->release_ms;
    s->channels=p->channels;
    s->level=p->level;
    s->execution_mode=p->execution_mode;
    s->db_epsilon=p->db_epsilon;
    s->rms_db=p->rms_db;
    s->peak_db=p->peak_db;
}
static void default_parameters(orpheus_builtin_level_detect_Parameters* p) {
    memset(p, 0, sizeof(*p));
    p->mode=0;
    p->attack_ms=1.000000000e+01f;
    p->release_ms=1.000000000e+02f;
    p->channels=2;
    p->level=0.000000000e+00f;
    p->execution_mode=0;
    p->db_epsilon=2.220446050e-16f;
    p->rms_db=0.000000000e+00f;
    p->peak_db=0.000000000e+00f;
}
static int update_parameter(orpheus_builtin_level_detect_Parameters* p, const char* key, const OrpheusValue* value) {
    if (!p || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "mode") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<0 || value->value.i32>1) return ORPHEUS_ERR_INVALID_ARG;
        p->mode=value->value.i32;
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
    if (strcmp(key, "channels") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>32) return ORPHEUS_ERR_INVALID_ARG;
        p->channels=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "level") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->level=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "execution_mode") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<0 || value->value.i32>1) return ORPHEUS_ERR_INVALID_ARG;
        p->execution_mode=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "db_epsilon") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->db_epsilon=value->value.f32;
        return ORPHEUS_OK;
    }
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
    return ORPHEUS_ERR_NOT_FOUND;
}
static int parameter_value(const orpheus_builtin_level_detect_Parameters* p, const char* key, OrpheusValue* value) {
    (void)p; (void)key; (void)value;
    if (strcmp(key, "mode")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->mode; return ORPHEUS_OK;
    }
    if (strcmp(key, "attack_ms")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->attack_ms; return ORPHEUS_OK;
    }
    if (strcmp(key, "release_ms")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->release_ms; return ORPHEUS_OK;
    }
    if (strcmp(key, "channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->channels; return ORPHEUS_OK;
    }
    if (strcmp(key, "level")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->level; return ORPHEUS_OK;
    }
    if (strcmp(key, "execution_mode")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->execution_mode; return ORPHEUS_OK;
    }
    if (strcmp(key, "db_epsilon")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->db_epsilon; return ORPHEUS_OK;
    }
    if (strcmp(key, "rms_db")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->rms_db; return ORPHEUS_OK;
    }
    if (strcmp(key, "peak_db")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->peak_db; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int set_parameter(void* state, const char* key, const OrpheusValue* value) {
    orpheus_builtin_level_detect_State* s=(orpheus_builtin_level_detect_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "level")==0) return ORPHEUS_ERR_UNSUPPORTED;
    if (strcmp(key, "rms_db")==0) return ORPHEUS_ERR_UNSUPPORTED;
    if (strcmp(key, "peak_db")==0) return ORPHEUS_ERR_UNSUPPORTED;
    orpheus_builtin_level_detect_Parameters before, candidate; snapshot_parameters(s, &before); candidate=before;
    int result=update_parameter(&candidate, key, value);
    if (result!=ORPHEUS_OK) return result;
    result=orpheus_builtin_level_detect_validate(&candidate); if (result!=ORPHEUS_OK) return result;
    OrpheusValue checked; result=parameter_value(&candidate, key, &checked);
    if (result!=ORPHEUS_OK) return result;
    result=orpheus_builtin_level_detect_set_parameter(s, key, &checked);
    if (result!=ORPHEUS_OK) { commit_parameters(s, &before); return result; }
    orpheus_builtin_level_detect_Parameters after; snapshot_parameters(s, &after);
    result=update_parameter(&after, key, &checked);
    if (result!=ORPHEUS_OK) { commit_parameters(s, &before); return result; }
    candidate=after;
    s->_orpheus_cleanup_pending=true;
    commit_parameters(s, &candidate); return ORPHEUS_OK;
}
static int get_parameter(void* state, const char* key, OrpheusValue* value) {
    orpheus_builtin_level_detect_State* s=(orpheus_builtin_level_detect_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "mode")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->mode; return ORPHEUS_OK;
    }
    if (strcmp(key, "attack_ms")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->attack_ms; return ORPHEUS_OK;
    }
    if (strcmp(key, "release_ms")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->release_ms; return ORPHEUS_OK;
    }
    if (strcmp(key, "channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->channels; return ORPHEUS_OK;
    }
    if (strcmp(key, "level")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->level; return ORPHEUS_OK;
    }
    if (strcmp(key, "execution_mode")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->execution_mode; return ORPHEUS_OK;
    }
    if (strcmp(key, "db_epsilon")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->db_epsilon; return ORPHEUS_OK;
    }
    if (strcmp(key, "rms_db")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->rms_db; return ORPHEUS_OK;
    }
    if (strcmp(key, "peak_db")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->peak_db; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int configure_parameters(orpheus_builtin_level_detect_Parameters* p, const OrpheusConfig* config) {
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
    return orpheus_builtin_level_detect_validate(p);
}
static int cleanup(orpheus_builtin_level_detect_State* s) {
    int result=ORPHEUS_OK;
    if (s->_orpheus_cleanup_pending) {
        s->_orpheus_cleanup_pending=false;
        memset(&s->algorithm, 0, sizeof(s->algorithm));
    }
    return result;
}
static int prepare(void* state, const OrpheusConfig* config) {
    orpheus_builtin_level_detect_State* s=(orpheus_builtin_level_detect_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_level_detect_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    result=cleanup(s); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate);
    s->_orpheus_cleanup_pending=true;
    result=orpheus_builtin_level_detect_prepare(s, config);
    if (result!=ORPHEUS_OK) { (void)cleanup(s); return result; }
    return ORPHEUS_OK;
}
static int create(void** state, const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=NULL;
    if (!config || !config->state_block) return ORPHEUS_ERR_INVALID_ARG;
    if ((uintptr_t)config->state_block % descriptor.alignment) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_level_detect_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    orpheus_builtin_level_detect_State* s=(orpheus_builtin_level_detect_State*)config->state_block;
    memset(s, 0, sizeof(*s)); commit_parameters(s, &candidate);
    s->_orpheus_alive=true; *state=s; return ORPHEUS_OK;
}
static int destroy(void* state) {
    orpheus_builtin_level_detect_State* s=(orpheus_builtin_level_detect_State*)state;
    if (!s) return ORPHEUS_ERR_INVALID_ARG;
    if (!s->_orpheus_alive) return ORPHEUS_OK;
    int result=cleanup(s); s->_orpheus_alive=false; return result;
}
static int reset(void* state) {
    orpheus_builtin_level_detect_State* s=(orpheus_builtin_level_detect_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_builtin_level_detect_reset(s);
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    orpheus_builtin_level_detect_State* s=(orpheus_builtin_level_detect_State*)state;
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_SLOT(reg, s, mode, ORPHEUS_SLOT_SETTING, "mode", "模式", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_i32=0, .max_i32=1)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, attack_ms, ORPHEUS_SLOT_SETTING, "attack_ms", "启动时间", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="ms", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=1.000000000e-01f, .max_f32=1.000000000e+03f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, release_ms, ORPHEUS_SLOT_SETTING, "release_ms", "释放时间", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="ms", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=1.000000000e+00f, .max_f32=5.000000000e+03f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=1, .max_i32=32)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, level, ORPHEUS_SLOT_PROBE, "level", "电平", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .flags=ORPHEUS_SLOT_READBACK)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, execution_mode, ORPHEUS_SLOT_SETTING, "execution_mode", "执行模式（0平滑包络，1块级双dB统计）", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=0, .max_i32=1)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, db_epsilon, ORPHEUS_SLOT_SETTING, "db_epsilon", "dB统计正偏置", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, rms_db, ORPHEUS_SLOT_PROBE, "rms_db", "最大通道均方dB", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .flags=ORPHEUS_SLOT_READBACK)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, peak_db, ORPHEUS_SLOT_PROBE, "peak_db", "全块峰值dB", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .flags=ORPHEUS_SLOT_READBACK)==ORPHEUS_SLOT_ID_INVALID)
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
    orpheus_builtin_level_detect_State* s=(orpheus_builtin_level_detect_State*)state;
    orpheus_builtin_level_detect_IO io={0};
    if (!s || !s->_orpheus_alive || !ctx) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t _orpheus_input_count=0, _orpheus_output_count=0;
    _orpheus_input_count+=1;
    _orpheus_output_count+=1;
    int64_t _orpheus_count_rms=s->execution_mode;
    if (_orpheus_count_rms < 0 || _orpheus_count_rms > 1)
        return ORPHEUS_ERR_INVALID_ARG;
    io.rms_count=(uint32_t)_orpheus_count_rms;
    _orpheus_output_count+=io.rms_count;
    int64_t _orpheus_count_peak=s->execution_mode;
    if (_orpheus_count_peak < 0 || _orpheus_count_peak > 1)
        return ORPHEUS_ERR_INVALID_ARG;
    io.peak_count=(uint32_t)_orpheus_count_peak;
    _orpheus_output_count+=io.peak_count;
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
    double _orpheus_payload_rms=(double)(1);
    if (!isfinite(_orpheus_payload_rms) || _orpheus_payload_rms < 1
            || _orpheus_payload_rms > UINT32_MAX || _orpheus_payload_rms != (uint32_t)_orpheus_payload_rms)
        return ORPHEUS_ERR_INVALID_ARG;
    for (uint32_t _orpheus_i=0; _orpheus_i<io.rms_count; ++_orpheus_i) {
    io.rms[_orpheus_i].active=true;
    io.rms[_orpheus_i].drive_port_no=2u+_orpheus_i;
    io.rms[_orpheus_i].buffer=(ctx->outputs && ctx->output_count > (_orpheus_output_slot+_orpheus_i)) ? ctx->outputs[_orpheus_output_slot+_orpheus_i] : NULL;
    if (!valid_buffer(io.rms[_orpheus_i].buffer, (uint32_t)(1),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.rms[_orpheus_i].samples=io.rms[_orpheus_i].buffer ? (float*)io.rms[_orpheus_i].buffer->data : NULL;
    if (io.rms[_orpheus_i].buffer) {
        if ((uint32_t)_orpheus_payload_rms > io.rms[_orpheus_i].buffer->frame_capacity) return ORPHEUS_ERR_INVALID_ARG;
        io.rms[_orpheus_i].buffer->frame_count=(uint32_t)_orpheus_payload_rms;
    }
    io.rms[_orpheus_i].delivery=(OrpheusDelivery){.buffer=io.rms[_orpheus_i].buffer, .frame_index=ctx->frame_index,
        .epoch=ctx->epoch, .timeline_flags=ctx->timeline_flags};
    }
    _orpheus_output_slot+=io.rms_count;
    double _orpheus_payload_peak=(double)(1);
    if (!isfinite(_orpheus_payload_peak) || _orpheus_payload_peak < 1
            || _orpheus_payload_peak > UINT32_MAX || _orpheus_payload_peak != (uint32_t)_orpheus_payload_peak)
        return ORPHEUS_ERR_INVALID_ARG;
    for (uint32_t _orpheus_i=0; _orpheus_i<io.peak_count; ++_orpheus_i) {
    io.peak[_orpheus_i].active=true;
    io.peak[_orpheus_i].drive_port_no=3u+_orpheus_i;
    io.peak[_orpheus_i].buffer=(ctx->outputs && ctx->output_count > (_orpheus_output_slot+_orpheus_i)) ? ctx->outputs[_orpheus_output_slot+_orpheus_i] : NULL;
    if (!valid_buffer(io.peak[_orpheus_i].buffer, (uint32_t)(1),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.peak[_orpheus_i].samples=io.peak[_orpheus_i].buffer ? (float*)io.peak[_orpheus_i].buffer->data : NULL;
    if (io.peak[_orpheus_i].buffer) {
        if ((uint32_t)_orpheus_payload_peak > io.peak[_orpheus_i].buffer->frame_capacity) return ORPHEUS_ERR_INVALID_ARG;
        io.peak[_orpheus_i].buffer->frame_count=(uint32_t)_orpheus_payload_peak;
    }
    io.peak[_orpheus_i].delivery=(OrpheusDelivery){.buffer=io.peak[_orpheus_i].buffer, .frame_index=ctx->frame_index,
        .epoch=ctx->epoch, .timeline_flags=ctx->timeline_flags};
    }
    _orpheus_output_slot+=io.peak_count;
    return orpheus_builtin_level_detect_algorithm(s, &io, ctx);
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
