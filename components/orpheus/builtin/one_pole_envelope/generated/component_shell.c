#include "component_state.h"
#include <string.h>
#include <math.h>

static const OrpheusPort ports[] = {
    {.id="start", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_BULK, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=1, .is_variable=true, .channels_param="channels"},
    {.id="target", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_BULK, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=1, .is_variable=true, .channels_param="channels"},
    {.id="alpha", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_BULK, .sample_format=ORPHEUS_FORMAT_F32, .channels=1, .sample_rate=0, .block_size=1, .is_variable=false, .channels_param=NULL},
    {.id="freeze", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_BULK, .sample_format=ORPHEUS_FORMAT_I32, .channels=1, .sample_rate=0, .block_size=1, .is_variable=false, .channels_param=NULL},
    {.id="in", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
    {.id="gains", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_BULK, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=1, .is_variable=true, .channels_param="channels"},
    {.id="audio", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
};

static const OrpheusParameter params[] = {
    {.id="channels", .name="包络通道数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=20}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=1, .max_i32=128},
};

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.one_pole_envelope", .version="1.0.0", .abi_version=7,
    .ports=ports, .port_count=8,
    .params=params, .param_count=1,
    .state_size=sizeof(orpheus_builtin_one_pole_envelope_State), .alignment=_Alignof(orpheus_builtin_one_pole_envelope_State),
    .scratch_size=0, .latency_samples=0,
    .realtime_safe=true, .supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

typedef struct orpheus_builtin_one_pole_envelope_Parameters {
    int32_t channels;
} orpheus_builtin_one_pole_envelope_Parameters;
static void snapshot_parameters(const orpheus_builtin_one_pole_envelope_State* s, orpheus_builtin_one_pole_envelope_Parameters* p) {
    (void)s; memset(p, 0, sizeof(*p));
    p->channels=s->channels;
}
static void commit_parameters(orpheus_builtin_one_pole_envelope_State* s, const orpheus_builtin_one_pole_envelope_Parameters* p) {
    (void)s; (void)p;
    s->channels=p->channels;
}
static void default_parameters(orpheus_builtin_one_pole_envelope_Parameters* p) {
    memset(p, 0, sizeof(*p));
    p->channels=20;
}
static int update_parameter(orpheus_builtin_one_pole_envelope_Parameters* p, const char* key, const OrpheusValue* value) {
    if (!p || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "channels") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>128) return ORPHEUS_ERR_INVALID_ARG;
        p->channels=value->value.i32;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int set_parameter(void* state, const char* key, const OrpheusValue* value) {
    orpheus_builtin_one_pole_envelope_State* s=(orpheus_builtin_one_pole_envelope_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_one_pole_envelope_Parameters before, candidate; snapshot_parameters(s, &before); candidate=before;
    int result=update_parameter(&candidate, key, value);
    if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate); return ORPHEUS_OK;
}
static int get_parameter(void* state, const char* key, OrpheusValue* value) {
    orpheus_builtin_one_pole_envelope_State* s=(orpheus_builtin_one_pole_envelope_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->channels; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int configure_parameters(orpheus_builtin_one_pole_envelope_Parameters* p, const OrpheusConfig* config) {
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
static int cleanup(orpheus_builtin_one_pole_envelope_State* s) {
    int result=ORPHEUS_OK;
    if (s->_orpheus_cleanup_pending) {
        s->_orpheus_cleanup_pending=false;
        memset(&s->algorithm, 0, sizeof(s->algorithm));
    }
    return result;
}
static int prepare(void* state, const OrpheusConfig* config) {
    orpheus_builtin_one_pole_envelope_State* s=(orpheus_builtin_one_pole_envelope_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_one_pole_envelope_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    result=cleanup(s); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate);
    s->_orpheus_cleanup_pending=true;
    return ORPHEUS_OK;
}
static int create(void** state, const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=NULL;
    if (!config || !config->state_block) return ORPHEUS_ERR_INVALID_ARG;
    if ((uintptr_t)config->state_block % descriptor.alignment) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_one_pole_envelope_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    orpheus_builtin_one_pole_envelope_State* s=(orpheus_builtin_one_pole_envelope_State*)config->state_block;
    memset(s, 0, sizeof(*s)); commit_parameters(s, &candidate);
    s->_orpheus_alive=true; *state=s; return ORPHEUS_OK;
}
static int destroy(void* state) {
    orpheus_builtin_one_pole_envelope_State* s=(orpheus_builtin_one_pole_envelope_State*)state;
    if (!s) return ORPHEUS_ERR_INVALID_ARG;
    if (!s->_orpheus_alive) return ORPHEUS_OK;
    int result=cleanup(s); s->_orpheus_alive=false; return result;
}
static int reset(void* state) {
    orpheus_builtin_one_pole_envelope_State* s=(orpheus_builtin_one_pole_envelope_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    memset(&s->algorithm, 0, sizeof(s->algorithm)); return ORPHEUS_OK;
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    orpheus_builtin_one_pole_envelope_State* s=(orpheus_builtin_one_pole_envelope_State*)state;
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "包络通道数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=1, .max_i32=128)==ORPHEUS_SLOT_ID_INVALID)
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
    orpheus_builtin_one_pole_envelope_State* s=(orpheus_builtin_one_pole_envelope_State*)state;
    orpheus_builtin_one_pole_envelope_IO io={0};
    if (!s || !s->_orpheus_alive || !ctx) return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_payload_start=(double)(1);
    if (!isfinite(_orpheus_payload_start) || _orpheus_payload_start < 1
            || _orpheus_payload_start > UINT32_MAX || _orpheus_payload_start != (uint32_t)_orpheus_payload_start)
        return ORPHEUS_ERR_INVALID_ARG;
    io.start.input_index=0;
    io.start.buffer=(ctx->inputs && ctx->input_count > (0)) ? ctx->inputs[0] : NULL;
    if (!valid_buffer(io.start.buffer, (uint32_t)(s->channels),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.start.samples=io.start.buffer ? (const float*)io.start.buffer->data : NULL;
    if (io.start.buffer && io.start.buffer->frame_count != (uint32_t)_orpheus_payload_start)
        return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_payload_target=(double)(1);
    if (!isfinite(_orpheus_payload_target) || _orpheus_payload_target < 1
            || _orpheus_payload_target > UINT32_MAX || _orpheus_payload_target != (uint32_t)_orpheus_payload_target)
        return ORPHEUS_ERR_INVALID_ARG;
    io.target.input_index=1;
    io.target.buffer=(ctx->inputs && ctx->input_count > (1)) ? ctx->inputs[1] : NULL;
    if (!valid_buffer(io.target.buffer, (uint32_t)(s->channels),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.target.samples=io.target.buffer ? (const float*)io.target.buffer->data : NULL;
    if (io.target.buffer && io.target.buffer->frame_count != (uint32_t)_orpheus_payload_target)
        return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_payload_alpha=(double)(1);
    if (!isfinite(_orpheus_payload_alpha) || _orpheus_payload_alpha < 1
            || _orpheus_payload_alpha > UINT32_MAX || _orpheus_payload_alpha != (uint32_t)_orpheus_payload_alpha)
        return ORPHEUS_ERR_INVALID_ARG;
    io.alpha.input_index=2;
    io.alpha.buffer=(ctx->inputs && ctx->input_count > (2)) ? ctx->inputs[2] : NULL;
    if (!valid_buffer(io.alpha.buffer, (uint32_t)(1),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.alpha.samples=io.alpha.buffer ? (const float*)io.alpha.buffer->data : NULL;
    if (io.alpha.buffer && io.alpha.buffer->frame_count != (uint32_t)_orpheus_payload_alpha)
        return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_payload_freeze=(double)(1);
    if (!isfinite(_orpheus_payload_freeze) || _orpheus_payload_freeze < 1
            || _orpheus_payload_freeze > UINT32_MAX || _orpheus_payload_freeze != (uint32_t)_orpheus_payload_freeze)
        return ORPHEUS_ERR_INVALID_ARG;
    io.freeze.input_index=3;
    io.freeze.buffer=(ctx->inputs && ctx->input_count > (3)) ? ctx->inputs[3] : NULL;
    if (!valid_buffer(io.freeze.buffer, (uint32_t)(1),
        ORPHEUS_FORMAT_I32, sizeof(int32_t), _Alignof(int32_t)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.freeze.samples=io.freeze.buffer ? (const int32_t*)io.freeze.buffer->data : NULL;
    if (io.freeze.buffer && io.freeze.buffer->frame_count != (uint32_t)_orpheus_payload_freeze)
        return ORPHEUS_ERR_INVALID_ARG;
    io.in.input_index=4;
    io.in.buffer=(ctx->inputs && ctx->input_count > (4)) ? ctx->inputs[4] : NULL;
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
    double _orpheus_payload_gains=(double)(1);
    if (!isfinite(_orpheus_payload_gains) || _orpheus_payload_gains < 1
            || _orpheus_payload_gains > UINT32_MAX || _orpheus_payload_gains != (uint32_t)_orpheus_payload_gains)
        return ORPHEUS_ERR_INVALID_ARG;
    io.gains.active=true;
    io.gains.drive_port_no=2u;
    io.gains.buffer=(ctx->outputs && ctx->output_count > (1)) ? ctx->outputs[1] : NULL;
    if (!valid_buffer(io.gains.buffer, (uint32_t)(s->channels),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.gains.samples=io.gains.buffer ? (float*)io.gains.buffer->data : NULL;
    if (io.gains.buffer) {
        if ((uint32_t)_orpheus_payload_gains > io.gains.buffer->frame_capacity) return ORPHEUS_ERR_INVALID_ARG;
        io.gains.buffer->frame_count=(uint32_t)_orpheus_payload_gains;
    }
    io.gains.delivery=(OrpheusDelivery){.buffer=io.gains.buffer, .frame_index=ctx->frame_index,
        .epoch=ctx->epoch, .timeline_flags=ctx->timeline_flags};
    io.audio.active=true;
    io.audio.drive_port_no=3u;
    io.audio.buffer=(ctx->outputs && ctx->output_count > (2)) ? ctx->outputs[2] : NULL;
    if (!valid_buffer(io.audio.buffer, (uint32_t)(s->channels),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.audio.samples=io.audio.buffer ? (float*)io.audio.buffer->data : NULL;
    if (io.audio.buffer) {
        if (ctx->frame_count > io.audio.buffer->frame_capacity) return ORPHEUS_ERR_INVALID_ARG;
        io.audio.buffer->frame_count=ctx->frame_count;
    }
    io.audio.delivery=(OrpheusDelivery){.buffer=io.audio.buffer, .frame_index=ctx->frame_index,
        .epoch=ctx->epoch, .timeline_flags=ctx->timeline_flags};
    return orpheus_builtin_one_pole_envelope_algorithm(s, &io, ctx);
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
