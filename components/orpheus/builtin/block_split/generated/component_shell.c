#include "component_state.h"
#include <string.h>
#include <math.h>

static const OrpheusPort ports[] = {
    {.id="in", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=((double)(0) * (double)(0)), .is_variable=true, .channels_param="channels"},
    {.id="tick", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_TRIGGER, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=false, .channels_param=NULL},
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
};

static const OrpheusParameter params[] = {
    {.id="channels", .name="通道数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=2}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=1, .max_i32=64},
    {.id="factor", .name="拆分份数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=4}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=1, .max_i32=64},
    {.id="output_frames", .name="每次读取帧数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=32}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=1, .max_i32=65536},
    {.id="available_frames", .name="待读帧数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=0}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=false, .affects_signature=false},
    {.id="underflows", .name="欠载次数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=0}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=false, .affects_signature=false},
    {.id="overflows", .name="未读覆盖次数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=0}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=false, .affects_signature=false},
};

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.block_split", .version="1.0.0", .abi_version=7,
    .ports=ports, .port_count=3,
    .params=params, .param_count=6,
    .state_size=sizeof(orpheus_builtin_block_split_State), .alignment=_Alignof(orpheus_builtin_block_split_State),
    .scratch_size=0, .latency_samples=0,
    .realtime_safe=true, .supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

static void snapshot_parameters(const orpheus_builtin_block_split_State* s, orpheus_builtin_block_split_Parameters* p) {
    (void)s; memset(p, 0, sizeof(*p));
    p->channels=s->channels;
    p->factor=s->factor;
    p->output_frames=s->output_frames;
    p->available_frames=s->available_frames;
    p->underflows=s->underflows;
    p->overflows=s->overflows;
}
static void commit_parameters(orpheus_builtin_block_split_State* s, const orpheus_builtin_block_split_Parameters* p) {
    (void)s; (void)p;
    s->channels=p->channels;
    s->factor=p->factor;
    s->output_frames=p->output_frames;
    s->available_frames=p->available_frames;
    s->underflows=p->underflows;
    s->overflows=p->overflows;
}
static void default_parameters(orpheus_builtin_block_split_Parameters* p) {
    memset(p, 0, sizeof(*p));
    p->channels=2;
    p->factor=4;
    p->output_frames=32;
    p->available_frames=0;
    p->underflows=0;
    p->overflows=0;
}
static int update_parameter(orpheus_builtin_block_split_Parameters* p, const char* key, const OrpheusValue* value) {
    if (!p || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "channels") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>64) return ORPHEUS_ERR_INVALID_ARG;
        p->channels=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "factor") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>64) return ORPHEUS_ERR_INVALID_ARG;
        p->factor=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "output_frames") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>65536) return ORPHEUS_ERR_INVALID_ARG;
        p->output_frames=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "available_frames") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        p->available_frames=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "underflows") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        p->underflows=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "overflows") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        p->overflows=value->value.i32;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int normalize_parameters(orpheus_builtin_block_split_Parameters* p, const char* key) {
    int result=orpheus_builtin_block_split_normalize(p, key);
    if (result!=ORPHEUS_OK) return result;
    {
        OrpheusValue value={.type=ORPHEUS_VALUE_INT,.value.i32=p->channels};
        result=update_parameter(p, "channels", &value);
        if (result!=ORPHEUS_OK) return result;
    }
    {
        OrpheusValue value={.type=ORPHEUS_VALUE_INT,.value.i32=p->factor};
        result=update_parameter(p, "factor", &value);
        if (result!=ORPHEUS_OK) return result;
    }
    {
        OrpheusValue value={.type=ORPHEUS_VALUE_INT,.value.i32=p->output_frames};
        result=update_parameter(p, "output_frames", &value);
        if (result!=ORPHEUS_OK) return result;
    }
    {
        OrpheusValue value={.type=ORPHEUS_VALUE_INT,.value.i32=p->available_frames};
        result=update_parameter(p, "available_frames", &value);
        if (result!=ORPHEUS_OK) return result;
    }
    {
        OrpheusValue value={.type=ORPHEUS_VALUE_INT,.value.i32=p->underflows};
        result=update_parameter(p, "underflows", &value);
        if (result!=ORPHEUS_OK) return result;
    }
    {
        OrpheusValue value={.type=ORPHEUS_VALUE_INT,.value.i32=p->overflows};
        result=update_parameter(p, "overflows", &value);
        if (result!=ORPHEUS_OK) return result;
    }
    return ORPHEUS_OK;
}
static int parameter_value(const orpheus_builtin_block_split_Parameters* p, const char* key, OrpheusValue* value) {
    (void)p; (void)key; (void)value;
    if (strcmp(key, "channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->channels; return ORPHEUS_OK;
    }
    if (strcmp(key, "factor")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->factor; return ORPHEUS_OK;
    }
    if (strcmp(key, "output_frames")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->output_frames; return ORPHEUS_OK;
    }
    if (strcmp(key, "available_frames")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->available_frames; return ORPHEUS_OK;
    }
    if (strcmp(key, "underflows")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->underflows; return ORPHEUS_OK;
    }
    if (strcmp(key, "overflows")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->overflows; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int set_parameter(void* state, const char* key, const OrpheusValue* value) {
    orpheus_builtin_block_split_State* s=(orpheus_builtin_block_split_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "available_frames")==0) return ORPHEUS_ERR_UNSUPPORTED;
    if (strcmp(key, "underflows")==0) return ORPHEUS_ERR_UNSUPPORTED;
    if (strcmp(key, "overflows")==0) return ORPHEUS_ERR_UNSUPPORTED;
    orpheus_builtin_block_split_Parameters before, candidate; snapshot_parameters(s, &before); candidate=before;
    int result=update_parameter(&candidate, key, value);
    if (result!=ORPHEUS_OK) return result;
    result=normalize_parameters(&candidate, key); if (result!=ORPHEUS_OK) return result;
    OrpheusValue checked; result=parameter_value(&candidate, key, &checked);
    if (result!=ORPHEUS_OK) return result;
    result=orpheus_builtin_block_split_set_parameter(s, key, &checked);
    if (result!=ORPHEUS_OK) { commit_parameters(s, &before); return result; }
    orpheus_builtin_block_split_Parameters after; snapshot_parameters(s, &after);
    if (memcmp(&candidate.channels, &before.channels, sizeof(candidate.channels))!=0)
        memcpy(&after.channels, &candidate.channels, sizeof(after.channels));
    if (memcmp(&candidate.factor, &before.factor, sizeof(candidate.factor))!=0)
        memcpy(&after.factor, &candidate.factor, sizeof(after.factor));
    if (memcmp(&candidate.output_frames, &before.output_frames, sizeof(candidate.output_frames))!=0)
        memcpy(&after.output_frames, &candidate.output_frames, sizeof(after.output_frames));
    if (memcmp(&candidate.available_frames, &before.available_frames, sizeof(candidate.available_frames))!=0)
        memcpy(&after.available_frames, &candidate.available_frames, sizeof(after.available_frames));
    if (memcmp(&candidate.underflows, &before.underflows, sizeof(candidate.underflows))!=0)
        memcpy(&after.underflows, &candidate.underflows, sizeof(after.underflows));
    if (memcmp(&candidate.overflows, &before.overflows, sizeof(candidate.overflows))!=0)
        memcpy(&after.overflows, &candidate.overflows, sizeof(after.overflows));
    result=update_parameter(&after, key, &checked);
    if (result!=ORPHEUS_OK) { commit_parameters(s, &before); return result; }
    candidate=after;
    s->_orpheus_cleanup_pending=true;
    commit_parameters(s, &candidate); return ORPHEUS_OK;
}
static int get_parameter(void* state, const char* key, OrpheusValue* value) {
    orpheus_builtin_block_split_State* s=(orpheus_builtin_block_split_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->channels; return ORPHEUS_OK;
    }
    if (strcmp(key, "factor")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->factor; return ORPHEUS_OK;
    }
    if (strcmp(key, "output_frames")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->output_frames; return ORPHEUS_OK;
    }
    if (strcmp(key, "available_frames")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->available_frames; return ORPHEUS_OK;
    }
    if (strcmp(key, "underflows")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->underflows; return ORPHEUS_OK;
    }
    if (strcmp(key, "overflows")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->overflows; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int configure_parameters(orpheus_builtin_block_split_Parameters* p, const OrpheusConfig* config) {
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
    int normalized=normalize_parameters(p, NULL); if (normalized!=ORPHEUS_OK) return normalized;
    return ORPHEUS_OK;
}
static int cleanup(orpheus_builtin_block_split_State* s) {
    int result=ORPHEUS_OK;
    if (s->_orpheus_cleanup_pending) {
        s->_orpheus_cleanup_pending=false;
        result=orpheus_builtin_block_split_destroy(s);
        memset(&s->algorithm, 0, sizeof(s->algorithm));
    }
    return result;
}
static int prepare(void* state, const OrpheusConfig* config) {
    orpheus_builtin_block_split_State* s=(orpheus_builtin_block_split_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_block_split_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    result=cleanup(s); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate);
    s->_orpheus_cleanup_pending=true;
    result=orpheus_builtin_block_split_prepare(s, config);
    if (result!=ORPHEUS_OK) { (void)cleanup(s); return result; }
    return ORPHEUS_OK;
}
static int create(void** state, const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=NULL;
    if (!config || !config->state_block) return ORPHEUS_ERR_INVALID_ARG;
    if ((uintptr_t)config->state_block % descriptor.alignment) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_block_split_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    orpheus_builtin_block_split_State* s=(orpheus_builtin_block_split_State*)config->state_block;
    memset(s, 0, sizeof(*s)); commit_parameters(s, &candidate);
    s->_orpheus_alive=true; *state=s; return ORPHEUS_OK;
}
static int destroy(void* state) {
    orpheus_builtin_block_split_State* s=(orpheus_builtin_block_split_State*)state;
    if (!s) return ORPHEUS_ERR_INVALID_ARG;
    if (!s->_orpheus_alive) return ORPHEUS_OK;
    int result=cleanup(s); s->_orpheus_alive=false; return result;
}
static int reset(void* state) {
    orpheus_builtin_block_split_State* s=(orpheus_builtin_block_split_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_builtin_block_split_reset(s);
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    orpheus_builtin_block_split_State* s=(orpheus_builtin_block_split_State*)state;
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=1, .max_i32=64)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, factor, ORPHEUS_SLOT_SETTING, "factor", "拆分份数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=1, .max_i32=64)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, output_frames, ORPHEUS_SLOT_SETTING, "output_frames", "每次读取帧数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=1, .max_i32=65536)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, available_frames, ORPHEUS_SLOT_PROBE, "available_frames", "待读帧数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, underflows, ORPHEUS_SLOT_PROBE, "underflows", "欠载次数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, overflows, ORPHEUS_SLOT_PROBE, "overflows", "未读覆盖次数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK)==ORPHEUS_SLOT_ID_INVALID)
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
    orpheus_builtin_block_split_State* s=(orpheus_builtin_block_split_State*)state;
    orpheus_builtin_block_split_IO io={0};
    if (!s || !s->_orpheus_alive || !ctx) return ORPHEUS_ERR_INVALID_ARG;
    if (s->output_frames < 1 || s->output_frames > 65536) return ORPHEUS_ERR_INVALID_ARG;
    if (s->factor < 1 || s->factor > 64) return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_payload_in=(double)(((double)(s->output_frames) * (double)(s->factor)));
    if (!isfinite(_orpheus_payload_in) || _orpheus_payload_in < 1
            || _orpheus_payload_in > UINT32_MAX || _orpheus_payload_in != (uint32_t)_orpheus_payload_in)
        return ORPHEUS_ERR_INVALID_ARG;
    io.in.input_index=0;
    io.in.buffer=(ctx->inputs && ctx->input_count > (0)) ? ctx->inputs[0] : NULL;
    if (!valid_buffer(io.in.buffer, (uint32_t)(s->channels),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.in.samples=io.in.buffer ? (const float*)io.in.buffer->data : NULL;
    if (io.in.buffer && io.in.buffer->frame_count != (uint32_t)_orpheus_payload_in)
        return ORPHEUS_ERR_INVALID_ARG;
    io.tick.input_index=1;
    if (ctx->inputs && ctx->input_count>(1) && ctx->inputs[1])
        return ORPHEUS_ERR_INVALID_ARG;
    io.tick.delivery=ctx->trigger_input==(1) ? ctx->delivery : NULL;
    if (io.tick.delivery && io.tick.delivery->buffer) return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_payload_out=(double)(s->output_frames);
    if (!isfinite(_orpheus_payload_out) || _orpheus_payload_out < 1
            || _orpheus_payload_out > UINT32_MAX || _orpheus_payload_out != (uint32_t)_orpheus_payload_out)
        return ORPHEUS_ERR_INVALID_ARG;
    io.out.active=true;
    io.out.drive_port_no=1u;
    io.out.buffer=(ctx->outputs && ctx->output_count > (0)) ? ctx->outputs[0] : NULL;
    if (!valid_buffer(io.out.buffer, (uint32_t)(s->channels),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.out.samples=io.out.buffer ? (float*)io.out.buffer->data : NULL;
    if (io.out.buffer) {
        if ((uint32_t)_orpheus_payload_out > io.out.buffer->frame_capacity) return ORPHEUS_ERR_INVALID_ARG;
        io.out.buffer->frame_count=(uint32_t)_orpheus_payload_out;
    }
    io.out.delivery=(OrpheusDelivery){.buffer=io.out.buffer, .frame_index=ctx->frame_index,
        .epoch=ctx->epoch, .timeline_flags=ctx->timeline_flags};
    return orpheus_builtin_block_split_algorithm(s, &io, ctx);
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
