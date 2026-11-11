#include "component_state.h"
#include <string.h>
#include <math.h>

static const OrpheusPort ports[] = {
    {.id="in", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
};

static const OrpheusParameter params[] = {
    {.id="gain_index", .name="增益位置", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=1.280000000e+02f}, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_f32=0.000000000e+00f, .max_f32=2.550000000e+02f},
    {.id="offset", .name="中心位置", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=1.280000000e+02f}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="mutes_bass", .name="极端静音低音", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_f32=0.000000000e+00f, .max_f32=1.000000000e+00f},
    {.id="ramp_ms", .name="斜坡时间", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=3.000000000e+01f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="ms", .readback=true, .persistent=true, .affects_signature=false, .min_f32=0.000000000e+00f, .max_f32=5.000000000e+03f},
    {.id="ramp_db_per_s", .name="斜坡速率", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="dB/s", .readback=true, .persistent=true, .affects_signature=false, .min_f32=0.000000000e+00f, .max_f32=5.000000000e+04f},
    {.id="freeze", .name="冻结斜坡", .type=ORPHEUS_VALUE_BOOL, .default_value={.type=ORPHEUS_VALUE_BOOL, .value.b=false}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="center_mono_mode", .name="中置/低音目标策略", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str="attenuated"}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="apply_index_on_prepare", .name="启动时应用索引", .type=ORPHEUS_VALUE_BOOL, .default_value={.type=ORPHEUS_VALUE_BOOL, .value.b=true}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="preserve_state_on_reset", .name="Reset保留斜坡状态", .type=ORPHEUS_VALUE_BOOL, .default_value={.type=ORPHEUS_VALUE_BOOL, .value.b=false}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="channels", .name="通道数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=4}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=1, .max_i32=64},
    {.id="chan_map", .name="通道映射（-1静音）", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str="0,1,2,3"}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="table_idx", .name="索引表", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str="0,10,31,52,74,95,116,128,138,159,180,202,223,244,255"}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="table_db", .name="增益表", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str="-40,-30,-20,-10,0,0,0,0,0,0,0,-10,-20,-30,-40"}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
};

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.sleeping_beauty", .version="1.2.0", .abi_version=7,
    .ports=ports, .port_count=2,
    .params=params, .param_count=13,
    .state_size=sizeof(orpheus_builtin_sleeping_beauty_State), .alignment=_Alignof(orpheus_builtin_sleeping_beauty_State),
    .scratch_size=0, .latency_samples=0,
    .realtime_safe=true, .supports_inplace=true
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

static void snapshot_parameters(const orpheus_builtin_sleeping_beauty_State* s, orpheus_builtin_sleeping_beauty_Parameters* p) {
    (void)s; memset(p, 0, sizeof(*p));
    p->gain_index=s->gain_index;
    p->offset=s->offset;
    p->mutes_bass=s->mutes_bass;
    p->ramp_ms=s->ramp_ms;
    p->ramp_db_per_s=s->ramp_db_per_s;
    p->freeze=s->freeze;
    memcpy(p->center_mono_mode, s->center_mono_mode, sizeof(p->center_mono_mode));
    p->apply_index_on_prepare=s->apply_index_on_prepare;
    p->preserve_state_on_reset=s->preserve_state_on_reset;
    p->channels=s->channels;
    memcpy(p->chan_map, s->chan_map, sizeof(p->chan_map));
    memcpy(p->table_idx, s->table_idx, sizeof(p->table_idx));
    memcpy(p->table_db, s->table_db, sizeof(p->table_db));
}
static void commit_parameters(orpheus_builtin_sleeping_beauty_State* s, const orpheus_builtin_sleeping_beauty_Parameters* p) {
    (void)s; (void)p;
    s->gain_index=p->gain_index;
    s->offset=p->offset;
    s->mutes_bass=p->mutes_bass;
    s->ramp_ms=p->ramp_ms;
    s->ramp_db_per_s=p->ramp_db_per_s;
    s->freeze=p->freeze;
    memcpy(s->center_mono_mode, p->center_mono_mode, sizeof(s->center_mono_mode));
    s->apply_index_on_prepare=p->apply_index_on_prepare;
    s->preserve_state_on_reset=p->preserve_state_on_reset;
    s->channels=p->channels;
    memcpy(s->chan_map, p->chan_map, sizeof(s->chan_map));
    memcpy(s->table_idx, p->table_idx, sizeof(s->table_idx));
    memcpy(s->table_db, p->table_db, sizeof(s->table_db));
}
static void default_parameters(orpheus_builtin_sleeping_beauty_Parameters* p) {
    memset(p, 0, sizeof(*p));
    p->gain_index=1.280000000e+02f;
    p->offset=1.280000000e+02f;
    p->mutes_bass=0.000000000e+00f;
    p->ramp_ms=3.000000000e+01f;
    p->ramp_db_per_s=0.000000000e+00f;
    p->freeze=false;
    memcpy(p->center_mono_mode, "attenuated", 11);
    p->apply_index_on_prepare=true;
    p->preserve_state_on_reset=false;
    p->channels=4;
    memcpy(p->chan_map, "0,1,2,3", 8);
    memcpy(p->table_idx, "0,10,31,52,74,95,116,128,138,159,180,202,223,244,255", 53);
    memcpy(p->table_db, "-40,-30,-20,-10,0,0,0,0,0,0,0,-10,-20,-30,-40", 46);
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
static int update_parameter(orpheus_builtin_sleeping_beauty_Parameters* p, const char* key, const OrpheusValue* value) {
    if (!p || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
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
    if (strcmp(key, "mutes_bass") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<0.000000000e+00f || value->value.f32>1.000000000e+00f) return ORPHEUS_ERR_INVALID_ARG;
        p->mutes_bass=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "ramp_ms") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<0.000000000e+00f || value->value.f32>5.000000000e+03f) return ORPHEUS_ERR_INVALID_ARG;
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
    if (strcmp(key, "center_mono_mode") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->center_mono_mode), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->center_mono_mode)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->center_mono_mode, text, sizeof(text));
        return ORPHEUS_OK;
    }
    if (strcmp(key, "apply_index_on_prepare") == 0) {
        if (value->type!=ORPHEUS_VALUE_BOOL) return ORPHEUS_ERR_INVALID_ARG;
        p->apply_index_on_prepare=value->value.b;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "preserve_state_on_reset") == 0) {
        if (value->type!=ORPHEUS_VALUE_BOOL) return ORPHEUS_ERR_INVALID_ARG;
        p->preserve_state_on_reset=value->value.b;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "channels") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>64) return ORPHEUS_ERR_INVALID_ARG;
        p->channels=value->value.i32;
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
    if (strcmp(key, "table_idx") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->table_idx), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->table_idx)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->table_idx, text, sizeof(text));
        return ORPHEUS_OK;
    }
    if (strcmp(key, "table_db") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->table_db), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->table_db)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->table_db, text, sizeof(text));
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int parameter_value(const orpheus_builtin_sleeping_beauty_Parameters* p, const char* key, OrpheusValue* value) {
    (void)p; (void)key; (void)value;
    if (strcmp(key, "gain_index")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->gain_index; return ORPHEUS_OK;
    }
    if (strcmp(key, "offset")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->offset; return ORPHEUS_OK;
    }
    if (strcmp(key, "mutes_bass")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->mutes_bass; return ORPHEUS_OK;
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
    if (strcmp(key, "center_mono_mode")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=p->center_mono_mode; return ORPHEUS_OK;
    }
    if (strcmp(key, "apply_index_on_prepare")==0) {
        value->type=ORPHEUS_VALUE_BOOL; value->value.b=p->apply_index_on_prepare; return ORPHEUS_OK;
    }
    if (strcmp(key, "preserve_state_on_reset")==0) {
        value->type=ORPHEUS_VALUE_BOOL; value->value.b=p->preserve_state_on_reset; return ORPHEUS_OK;
    }
    if (strcmp(key, "channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->channels; return ORPHEUS_OK;
    }
    if (strcmp(key, "chan_map")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=p->chan_map; return ORPHEUS_OK;
    }
    if (strcmp(key, "table_idx")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=p->table_idx; return ORPHEUS_OK;
    }
    if (strcmp(key, "table_db")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=p->table_db; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int set_parameter(void* state, const char* key, const OrpheusValue* value) {
    orpheus_builtin_sleeping_beauty_State* s=(orpheus_builtin_sleeping_beauty_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_sleeping_beauty_Parameters before, candidate; snapshot_parameters(s, &before); candidate=before;
    int result=update_parameter(&candidate, key, value);
    if (result!=ORPHEUS_OK) return result;
    result=orpheus_builtin_sleeping_beauty_validate(&candidate); if (result!=ORPHEUS_OK) return result;
    OrpheusValue checked; result=parameter_value(&candidate, key, &checked);
    if (result!=ORPHEUS_OK) return result;
    result=orpheus_builtin_sleeping_beauty_set_parameter(s, key, &checked);
    if (result!=ORPHEUS_OK) { commit_parameters(s, &before); return result; }
    orpheus_builtin_sleeping_beauty_Parameters after; snapshot_parameters(s, &after);
    result=update_parameter(&after, key, &checked);
    if (result!=ORPHEUS_OK) { commit_parameters(s, &before); return result; }
    candidate=after;
    s->_orpheus_cleanup_pending=true;
    commit_parameters(s, &candidate); return ORPHEUS_OK;
}
static int get_parameter(void* state, const char* key, OrpheusValue* value) {
    orpheus_builtin_sleeping_beauty_State* s=(orpheus_builtin_sleeping_beauty_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "gain_index")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->gain_index; return ORPHEUS_OK;
    }
    if (strcmp(key, "offset")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->offset; return ORPHEUS_OK;
    }
    if (strcmp(key, "mutes_bass")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->mutes_bass; return ORPHEUS_OK;
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
    if (strcmp(key, "center_mono_mode")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->center_mono_mode; return ORPHEUS_OK;
    }
    if (strcmp(key, "apply_index_on_prepare")==0) {
        value->type=ORPHEUS_VALUE_BOOL; value->value.b=s->apply_index_on_prepare; return ORPHEUS_OK;
    }
    if (strcmp(key, "preserve_state_on_reset")==0) {
        value->type=ORPHEUS_VALUE_BOOL; value->value.b=s->preserve_state_on_reset; return ORPHEUS_OK;
    }
    if (strcmp(key, "channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->channels; return ORPHEUS_OK;
    }
    if (strcmp(key, "chan_map")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->chan_map; return ORPHEUS_OK;
    }
    if (strcmp(key, "table_idx")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->table_idx; return ORPHEUS_OK;
    }
    if (strcmp(key, "table_db")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->table_db; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int configure_parameters(orpheus_builtin_sleeping_beauty_Parameters* p, const OrpheusConfig* config) {
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
    return orpheus_builtin_sleeping_beauty_validate(p);
}
static int cleanup(orpheus_builtin_sleeping_beauty_State* s) {
    int result=ORPHEUS_OK;
    if (s->_orpheus_cleanup_pending) {
        s->_orpheus_cleanup_pending=false;
        memset(&s->algorithm, 0, sizeof(s->algorithm));
    }
    return result;
}
static int prepare(void* state, const OrpheusConfig* config) {
    orpheus_builtin_sleeping_beauty_State* s=(orpheus_builtin_sleeping_beauty_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_sleeping_beauty_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    result=cleanup(s); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate);
    s->_orpheus_cleanup_pending=true;
    result=orpheus_builtin_sleeping_beauty_prepare(s, config);
    if (result!=ORPHEUS_OK) { (void)cleanup(s); return result; }
    return ORPHEUS_OK;
}
static int create(void** state, const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=NULL;
    if (!config || !config->state_block) return ORPHEUS_ERR_INVALID_ARG;
    if ((uintptr_t)config->state_block % descriptor.alignment) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_sleeping_beauty_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    orpheus_builtin_sleeping_beauty_State* s=(orpheus_builtin_sleeping_beauty_State*)config->state_block;
    memset(s, 0, sizeof(*s)); commit_parameters(s, &candidate);
    s->_orpheus_alive=true; *state=s; return ORPHEUS_OK;
}
static int destroy(void* state) {
    orpheus_builtin_sleeping_beauty_State* s=(orpheus_builtin_sleeping_beauty_State*)state;
    if (!s) return ORPHEUS_ERR_INVALID_ARG;
    if (!s->_orpheus_alive) return ORPHEUS_OK;
    int result=cleanup(s); s->_orpheus_alive=false; return result;
}
static int reset(void* state) {
    orpheus_builtin_sleeping_beauty_State* s=(orpheus_builtin_sleeping_beauty_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_builtin_sleeping_beauty_reset(s);
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    orpheus_builtin_sleeping_beauty_State* s=(orpheus_builtin_sleeping_beauty_State*)state;
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_SLOT(reg, s, gain_index, ORPHEUS_SLOT_SETTING, "gain_index", "增益位置", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=0.000000000e+00f, .max_f32=2.550000000e+02f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, offset, ORPHEUS_SLOT_SETTING, "offset", "中心位置", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, mutes_bass, ORPHEUS_SLOT_SETTING, "mutes_bass", "极端静音低音", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=0.000000000e+00f, .max_f32=1.000000000e+00f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, ramp_ms, ORPHEUS_SLOT_SETTING, "ramp_ms", "斜坡时间", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="ms", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=0.000000000e+00f, .max_f32=5.000000000e+03f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, ramp_db_per_s, ORPHEUS_SLOT_SETTING, "ramp_db_per_s", "斜坡速率", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="dB/s", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=0.000000000e+00f, .max_f32=5.000000000e+04f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, freeze, ORPHEUS_SLOT_SETTING, "freeze", "冻结斜坡", ORPHEUS_VALUE_BOOL, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, center_mono_mode, ORPHEUS_SLOT_SETTING, "center_mono_mode", "中置/低音目标策略", ORPHEUS_VALUE_STRING, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, apply_index_on_prepare, ORPHEUS_SLOT_SETTING, "apply_index_on_prepare", "启动时应用索引", ORPHEUS_VALUE_BOOL, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, preserve_state_on_reset, ORPHEUS_SLOT_SETTING, "preserve_state_on_reset", "Reset保留斜坡状态", ORPHEUS_VALUE_BOOL, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=1, .max_i32=64)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, chan_map, ORPHEUS_SLOT_SETTING, "chan_map", "通道映射（-1静音）", ORPHEUS_VALUE_STRING, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, table_idx, ORPHEUS_SLOT_SETTING, "table_idx", "索引表", ORPHEUS_VALUE_STRING, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, table_db, ORPHEUS_SLOT_SETTING, "table_db", "增益表", ORPHEUS_VALUE_STRING, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
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
    orpheus_builtin_sleeping_beauty_State* s=(orpheus_builtin_sleeping_beauty_State*)state;
    orpheus_builtin_sleeping_beauty_IO io={0};
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
    return orpheus_builtin_sleeping_beauty_algorithm(s, &io, ctx);
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
