#include "component_state.h"
#include <string.h>
#include <math.h>

static const OrpheusPort ports[] = {
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
};

static const OrpheusParameter params[] = {
    {.id="progress", .name="扫频进度", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .readback=true, .persistent=false, .affects_signature=false},
    {.id="current_freq", .name="当前频率", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .readback=true, .persistent=false, .affects_signature=false},
    {.id="sample_rate", .name="采样率", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=48000}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="Hz", .readback=true, .persistent=true, .affects_signature=true, .min_i32=8000, .max_i32=192000},
    {.id="start_freq", .name="起始频率", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=2.000000000e+01f}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="Hz", .readback=true, .persistent=true, .affects_signature=false, .min_f32=1.000000000e+00f, .max_f32=2.000000000e+04f},
    {.id="end_freq", .name="结束频率", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=2.000000000e+04f}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="Hz", .readback=true, .persistent=true, .affects_signature=false, .min_f32=1.000000000e+00f, .max_f32=2.000000000e+04f},
    {.id="duration_s", .name="扫频时长", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=5.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="s", .readback=true, .persistent=true, .affects_signature=false, .min_f32=1.000000000e-01f, .max_f32=3.000000000e+02f},
    {.id="amplitude", .name="幅度", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=7.000000000e-01f}, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_f32=0.000000000e+00f, .max_f32=1.000000000e+00f},
    {.id="log_scale", .name="对数扫频", .type=ORPHEUS_VALUE_BOOL, .default_value={.type=ORPHEUS_VALUE_BOOL, .value.b=true}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="channels", .name="通道数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=2}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=1, .max_i32=32},
};

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.sweep_gen", .version="1.0.0", .abi_version=7,
    .ports=ports, .port_count=1,
    .params=params, .param_count=9,
    .state_size=sizeof(orpheus_builtin_sweep_gen_State), .alignment=_Alignof(orpheus_builtin_sweep_gen_State),
    .scratch_size=0, .latency_samples=0,
    .realtime_safe=true, .supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

typedef struct orpheus_builtin_sweep_gen_Parameters {
    float progress;
    float current_freq;
    int32_t sample_rate;
    float start_freq;
    float end_freq;
    float duration_s;
    float amplitude;
    bool log_scale;
    int32_t channels;
} orpheus_builtin_sweep_gen_Parameters;
static void snapshot_parameters(const orpheus_builtin_sweep_gen_State* s, orpheus_builtin_sweep_gen_Parameters* p) {
    (void)s; memset(p, 0, sizeof(*p));
    p->progress=s->progress;
    p->current_freq=s->current_freq;
    p->sample_rate=s->sample_rate;
    p->start_freq=s->start_freq;
    p->end_freq=s->end_freq;
    p->duration_s=s->duration_s;
    p->amplitude=s->amplitude;
    p->log_scale=s->log_scale;
    p->channels=s->channels;
}
static void commit_parameters(orpheus_builtin_sweep_gen_State* s, const orpheus_builtin_sweep_gen_Parameters* p) {
    (void)s; (void)p;
    s->progress=p->progress;
    s->current_freq=p->current_freq;
    s->sample_rate=p->sample_rate;
    s->start_freq=p->start_freq;
    s->end_freq=p->end_freq;
    s->duration_s=p->duration_s;
    s->amplitude=p->amplitude;
    s->log_scale=p->log_scale;
    s->channels=p->channels;
}
static void default_parameters(orpheus_builtin_sweep_gen_Parameters* p) {
    memset(p, 0, sizeof(*p));
    p->progress=0.000000000e+00f;
    p->current_freq=0.000000000e+00f;
    p->sample_rate=48000;
    p->start_freq=2.000000000e+01f;
    p->end_freq=2.000000000e+04f;
    p->duration_s=5.000000000e+00f;
    p->amplitude=7.000000000e-01f;
    p->log_scale=true;
    p->channels=2;
}
static int update_parameter(orpheus_builtin_sweep_gen_Parameters* p, const char* key, const OrpheusValue* value) {
    if (!p || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "progress") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->progress=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "current_freq") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->current_freq=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "sample_rate") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<8000 || value->value.i32>192000) return ORPHEUS_ERR_INVALID_ARG;
        p->sample_rate=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "start_freq") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<1.000000000e+00f || value->value.f32>2.000000000e+04f) return ORPHEUS_ERR_INVALID_ARG;
        p->start_freq=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "end_freq") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<1.000000000e+00f || value->value.f32>2.000000000e+04f) return ORPHEUS_ERR_INVALID_ARG;
        p->end_freq=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "duration_s") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<1.000000000e-01f || value->value.f32>3.000000000e+02f) return ORPHEUS_ERR_INVALID_ARG;
        p->duration_s=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "amplitude") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<0.000000000e+00f || value->value.f32>1.000000000e+00f) return ORPHEUS_ERR_INVALID_ARG;
        p->amplitude=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "log_scale") == 0) {
        if (value->type!=ORPHEUS_VALUE_BOOL) return ORPHEUS_ERR_INVALID_ARG;
        p->log_scale=value->value.b;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "channels") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>32) return ORPHEUS_ERR_INVALID_ARG;
        p->channels=value->value.i32;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int parameter_value(const orpheus_builtin_sweep_gen_Parameters* p, const char* key, OrpheusValue* value) {
    (void)p; (void)key; (void)value;
    if (strcmp(key, "progress")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->progress; return ORPHEUS_OK;
    }
    if (strcmp(key, "current_freq")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->current_freq; return ORPHEUS_OK;
    }
    if (strcmp(key, "sample_rate")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->sample_rate; return ORPHEUS_OK;
    }
    if (strcmp(key, "start_freq")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->start_freq; return ORPHEUS_OK;
    }
    if (strcmp(key, "end_freq")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->end_freq; return ORPHEUS_OK;
    }
    if (strcmp(key, "duration_s")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->duration_s; return ORPHEUS_OK;
    }
    if (strcmp(key, "amplitude")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=p->amplitude; return ORPHEUS_OK;
    }
    if (strcmp(key, "log_scale")==0) {
        value->type=ORPHEUS_VALUE_BOOL; value->value.b=p->log_scale; return ORPHEUS_OK;
    }
    if (strcmp(key, "channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->channels; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int set_parameter(void* state, const char* key, const OrpheusValue* value) {
    orpheus_builtin_sweep_gen_State* s=(orpheus_builtin_sweep_gen_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "progress")==0) return ORPHEUS_ERR_UNSUPPORTED;
    if (strcmp(key, "current_freq")==0) return ORPHEUS_ERR_UNSUPPORTED;
    orpheus_builtin_sweep_gen_Parameters before, candidate; snapshot_parameters(s, &before); candidate=before;
    int result=update_parameter(&candidate, key, value);
    if (result!=ORPHEUS_OK) return result;
    OrpheusValue checked; result=parameter_value(&candidate, key, &checked);
    if (result!=ORPHEUS_OK) return result;
    result=orpheus_builtin_sweep_gen_set_parameter(s, key, &checked);
    if (result!=ORPHEUS_OK) { commit_parameters(s, &before); return result; }
    orpheus_builtin_sweep_gen_Parameters after; snapshot_parameters(s, &after);
    result=update_parameter(&after, key, &checked);
    if (result!=ORPHEUS_OK) { commit_parameters(s, &before); return result; }
    candidate=after;
    s->_orpheus_cleanup_pending=true;
    commit_parameters(s, &candidate); return ORPHEUS_OK;
}
static int get_parameter(void* state, const char* key, OrpheusValue* value) {
    orpheus_builtin_sweep_gen_State* s=(orpheus_builtin_sweep_gen_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "progress")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->progress; return ORPHEUS_OK;
    }
    if (strcmp(key, "current_freq")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->current_freq; return ORPHEUS_OK;
    }
    if (strcmp(key, "sample_rate")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->sample_rate; return ORPHEUS_OK;
    }
    if (strcmp(key, "start_freq")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->start_freq; return ORPHEUS_OK;
    }
    if (strcmp(key, "end_freq")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->end_freq; return ORPHEUS_OK;
    }
    if (strcmp(key, "duration_s")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->duration_s; return ORPHEUS_OK;
    }
    if (strcmp(key, "amplitude")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->amplitude; return ORPHEUS_OK;
    }
    if (strcmp(key, "log_scale")==0) {
        value->type=ORPHEUS_VALUE_BOOL; value->value.b=s->log_scale; return ORPHEUS_OK;
    }
    if (strcmp(key, "channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->channels; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int configure_parameters(orpheus_builtin_sweep_gen_Parameters* p, const OrpheusConfig* config) {
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
static int cleanup(orpheus_builtin_sweep_gen_State* s) {
    int result=ORPHEUS_OK;
    if (s->_orpheus_cleanup_pending) {
        s->_orpheus_cleanup_pending=false;
        memset(&s->algorithm, 0, sizeof(s->algorithm));
    }
    return result;
}
static int prepare(void* state, const OrpheusConfig* config) {
    orpheus_builtin_sweep_gen_State* s=(orpheus_builtin_sweep_gen_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_sweep_gen_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    result=cleanup(s); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate);
    s->_orpheus_cleanup_pending=true;
    result=orpheus_builtin_sweep_gen_prepare(s, config);
    if (result!=ORPHEUS_OK) { (void)cleanup(s); return result; }
    return ORPHEUS_OK;
}
static int create(void** state, const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=NULL;
    if (!config || !config->state_block) return ORPHEUS_ERR_INVALID_ARG;
    if ((uintptr_t)config->state_block % descriptor.alignment) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_sweep_gen_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    orpheus_builtin_sweep_gen_State* s=(orpheus_builtin_sweep_gen_State*)config->state_block;
    memset(s, 0, sizeof(*s)); commit_parameters(s, &candidate);
    s->_orpheus_alive=true; *state=s; return ORPHEUS_OK;
}
static int destroy(void* state) {
    orpheus_builtin_sweep_gen_State* s=(orpheus_builtin_sweep_gen_State*)state;
    if (!s) return ORPHEUS_ERR_INVALID_ARG;
    if (!s->_orpheus_alive) return ORPHEUS_OK;
    int result=cleanup(s); s->_orpheus_alive=false; return result;
}
static int reset(void* state) {
    orpheus_builtin_sweep_gen_State* s=(orpheus_builtin_sweep_gen_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_builtin_sweep_gen_reset(s);
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    orpheus_builtin_sweep_gen_State* s=(orpheus_builtin_sweep_gen_State*)state;
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_SLOT(reg, s, progress, ORPHEUS_SLOT_PROBE, "progress", "扫频进度", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .flags=ORPHEUS_SLOT_READBACK)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, current_freq, ORPHEUS_SLOT_PROBE, "current_freq", "当前频率", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .flags=ORPHEUS_SLOT_READBACK)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, sample_rate, ORPHEUS_SLOT_SETTING, "sample_rate", "采样率", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="Hz", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=8000, .max_i32=192000)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, start_freq, ORPHEUS_SLOT_SETTING, "start_freq", "起始频率", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="Hz", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=1.000000000e+00f, .max_f32=2.000000000e+04f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, end_freq, ORPHEUS_SLOT_SETTING, "end_freq", "结束频率", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="Hz", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=1.000000000e+00f, .max_f32=2.000000000e+04f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, duration_s, ORPHEUS_SLOT_SETTING, "duration_s", "扫频时长", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="s", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=1.000000000e-01f, .max_f32=3.000000000e+02f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, amplitude, ORPHEUS_SLOT_SETTING, "amplitude", "幅度", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_SMOOTHED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=0.000000000e+00f, .max_f32=1.000000000e+00f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, log_scale, ORPHEUS_SLOT_SETTING, "log_scale", "对数扫频", ORPHEUS_VALUE_BOOL, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=1, .max_i32=32)==ORPHEUS_SLOT_ID_INVALID)
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
    orpheus_builtin_sweep_gen_State* s=(orpheus_builtin_sweep_gen_State*)state;
    orpheus_builtin_sweep_gen_IO io={0};
    if (!s || !s->_orpheus_alive || !ctx) return ORPHEUS_ERR_INVALID_ARG;
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
    return orpheus_builtin_sweep_gen_algorithm(s, &io, ctx);
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
