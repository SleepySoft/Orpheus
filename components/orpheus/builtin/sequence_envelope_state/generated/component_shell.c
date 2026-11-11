#include "component_state.h"
#include <string.h>
#include <math.h>

static const OrpheusPort ports[] = {
    {.id="targets", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_BULK, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=1, .is_variable=true, .channels_param="channels"},
    {.id="sequence", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_BULK, .sample_format=ORPHEUS_FORMAT_I32, .channels=1, .sample_rate=0, .block_size=1, .is_variable=false, .channels_param=NULL},
    {.id="coefficient", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_BULK, .sample_format=ORPHEUS_FORMAT_F32, .channels=1, .sample_rate=0, .block_size=1, .is_variable=false, .channels_param=NULL},
    {.id="feedback", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_BULK, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=1, .is_variable=true, .channels_param="channels"},
    {.id="start", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_BULK, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=1, .is_variable=true, .channels_param="channels"},
    {.id="accepted", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_BULK, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=1, .is_variable=true, .channels_param="channels"},
    {.id="alpha", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_BULK, .sample_format=ORPHEUS_FORMAT_F32, .channels=1, .sample_rate=0, .block_size=1, .is_variable=false, .channels_param=NULL},
    {.id="freeze", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_BULK, .sample_format=ORPHEUS_FORMAT_I32, .channels=1, .sample_rate=0, .block_size=1, .is_variable=false, .channels_param=NULL},
};

static const OrpheusParameter params[] = {
    {.id="channels", .name="包络通道数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=20}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=1, .max_i32=128},
    {.id="silent", .name="新偶序列增益下界", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=5.011872300e-07f}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
};

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.sequence_envelope_state", .version="1.0.0", .abi_version=7,
    .ports=ports, .port_count=8,
    .params=params, .param_count=2,
    .state_size=sizeof(orpheus_builtin_sequence_envelope_state_State), .alignment=_Alignof(orpheus_builtin_sequence_envelope_state_State),
    .scratch_size=0, .latency_samples=0,
    .realtime_safe=true, .supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

static void snapshot_parameters(const orpheus_builtin_sequence_envelope_state_State* s, orpheus_builtin_sequence_envelope_state_Parameters* p) {
    (void)s; memset(p, 0, sizeof(*p));
    p->channels=s->channels;
    p->silent=s->silent;
}
static void commit_parameters(orpheus_builtin_sequence_envelope_state_State* s, const orpheus_builtin_sequence_envelope_state_Parameters* p) {
    (void)s; (void)p;
    s->channels=p->channels;
    s->silent=p->silent;
}
static void default_parameters(orpheus_builtin_sequence_envelope_state_Parameters* p) {
    memset(p, 0, sizeof(*p));
    p->channels=20;
    p->silent=5.011872300e-07f;
}
static int update_parameter(orpheus_builtin_sequence_envelope_state_Parameters* p, const char* key, const OrpheusValue* value) {
    if (!p || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "channels") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>128) return ORPHEUS_ERR_INVALID_ARG;
        p->channels=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "silent") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        p->silent=value->value.f32;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int normalize_parameters(orpheus_builtin_sequence_envelope_state_Parameters* p, const char* key) {
    int result=orpheus_builtin_sequence_envelope_state_normalize(p, key);
    if (result!=ORPHEUS_OK) return result;
    {
        OrpheusValue value={.type=ORPHEUS_VALUE_INT,.value.i32=p->channels};
        result=update_parameter(p, "channels", &value);
        if (result!=ORPHEUS_OK) return result;
    }
    {
        OrpheusValue value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=p->silent};
        result=update_parameter(p, "silent", &value);
        if (result!=ORPHEUS_OK) return result;
    }
    return ORPHEUS_OK;
}
static int set_parameter(void* state, const char* key, const OrpheusValue* value) {
    orpheus_builtin_sequence_envelope_state_State* s=(orpheus_builtin_sequence_envelope_state_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_sequence_envelope_state_Parameters before, candidate; snapshot_parameters(s, &before); candidate=before;
    int result=update_parameter(&candidate, key, value);
    if (result!=ORPHEUS_OK) return result;
    result=normalize_parameters(&candidate, key); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate); return ORPHEUS_OK;
}
static int get_parameter(void* state, const char* key, OrpheusValue* value) {
    orpheus_builtin_sequence_envelope_state_State* s=(orpheus_builtin_sequence_envelope_state_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->channels; return ORPHEUS_OK;
    }
    if (strcmp(key, "silent")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->silent; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int configure_parameters(orpheus_builtin_sequence_envelope_state_Parameters* p, const OrpheusConfig* config) {
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
static int cleanup(orpheus_builtin_sequence_envelope_state_State* s) {
    int result=ORPHEUS_OK;
    if (s->_orpheus_cleanup_pending) {
        s->_orpheus_cleanup_pending=false;
        memset(&s->algorithm, 0, sizeof(s->algorithm));
    }
    return result;
}
static int prepare(void* state, const OrpheusConfig* config) {
    orpheus_builtin_sequence_envelope_state_State* s=(orpheus_builtin_sequence_envelope_state_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_sequence_envelope_state_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    result=cleanup(s); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate);
    s->_orpheus_cleanup_pending=true;
    result=orpheus_builtin_sequence_envelope_state_prepare(s, config);
    if (result!=ORPHEUS_OK) { (void)cleanup(s); return result; }
    return ORPHEUS_OK;
}
static int create(void** state, const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=NULL;
    if (!config || !config->state_block) return ORPHEUS_ERR_INVALID_ARG;
    if ((uintptr_t)config->state_block % descriptor.alignment) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_sequence_envelope_state_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    orpheus_builtin_sequence_envelope_state_State* s=(orpheus_builtin_sequence_envelope_state_State*)config->state_block;
    memset(s, 0, sizeof(*s)); commit_parameters(s, &candidate);
    s->_orpheus_alive=true; *state=s; return ORPHEUS_OK;
}
static int destroy(void* state) {
    orpheus_builtin_sequence_envelope_state_State* s=(orpheus_builtin_sequence_envelope_state_State*)state;
    if (!s) return ORPHEUS_ERR_INVALID_ARG;
    if (!s->_orpheus_alive) return ORPHEUS_OK;
    int result=cleanup(s); s->_orpheus_alive=false; return result;
}
static int reset(void* state) {
    orpheus_builtin_sequence_envelope_state_State* s=(orpheus_builtin_sequence_envelope_state_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_builtin_sequence_envelope_state_reset(s);
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    orpheus_builtin_sequence_envelope_state_State* s=(orpheus_builtin_sequence_envelope_state_State*)state;
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "包络通道数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=1, .max_i32=128)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, silent, ORPHEUS_SLOT_SETTING, "silent", "新偶序列增益下界", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
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
    orpheus_builtin_sequence_envelope_state_State* s=(orpheus_builtin_sequence_envelope_state_State*)state;
    orpheus_builtin_sequence_envelope_state_IO io={0};
    if (!s || !s->_orpheus_alive || !ctx) return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_payload_targets=(double)(1);
    if (!isfinite(_orpheus_payload_targets) || _orpheus_payload_targets < 1
            || _orpheus_payload_targets > UINT32_MAX || _orpheus_payload_targets != (uint32_t)_orpheus_payload_targets)
        return ORPHEUS_ERR_INVALID_ARG;
    io.targets.input_index=0;
    io.targets.buffer=(ctx->inputs && ctx->input_count > (0)) ? ctx->inputs[0] : NULL;
    if (!valid_buffer(io.targets.buffer, (uint32_t)(s->channels),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.targets.samples=io.targets.buffer ? (const float*)io.targets.buffer->data : NULL;
    if (io.targets.buffer && io.targets.buffer->frame_count != (uint32_t)_orpheus_payload_targets)
        return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_payload_sequence=(double)(1);
    if (!isfinite(_orpheus_payload_sequence) || _orpheus_payload_sequence < 1
            || _orpheus_payload_sequence > UINT32_MAX || _orpheus_payload_sequence != (uint32_t)_orpheus_payload_sequence)
        return ORPHEUS_ERR_INVALID_ARG;
    io.sequence.input_index=1;
    io.sequence.buffer=(ctx->inputs && ctx->input_count > (1)) ? ctx->inputs[1] : NULL;
    if (!valid_buffer(io.sequence.buffer, (uint32_t)(1),
        ORPHEUS_FORMAT_I32, sizeof(int32_t), _Alignof(int32_t)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.sequence.samples=io.sequence.buffer ? (const int32_t*)io.sequence.buffer->data : NULL;
    if (io.sequence.buffer && io.sequence.buffer->frame_count != (uint32_t)_orpheus_payload_sequence)
        return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_payload_coefficient=(double)(1);
    if (!isfinite(_orpheus_payload_coefficient) || _orpheus_payload_coefficient < 1
            || _orpheus_payload_coefficient > UINT32_MAX || _orpheus_payload_coefficient != (uint32_t)_orpheus_payload_coefficient)
        return ORPHEUS_ERR_INVALID_ARG;
    io.coefficient.input_index=2;
    io.coefficient.buffer=(ctx->inputs && ctx->input_count > (2)) ? ctx->inputs[2] : NULL;
    if (!valid_buffer(io.coefficient.buffer, (uint32_t)(1),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.coefficient.samples=io.coefficient.buffer ? (const float*)io.coefficient.buffer->data : NULL;
    if (io.coefficient.buffer && io.coefficient.buffer->frame_count != (uint32_t)_orpheus_payload_coefficient)
        return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_payload_feedback=(double)(1);
    if (!isfinite(_orpheus_payload_feedback) || _orpheus_payload_feedback < 1
            || _orpheus_payload_feedback > UINT32_MAX || _orpheus_payload_feedback != (uint32_t)_orpheus_payload_feedback)
        return ORPHEUS_ERR_INVALID_ARG;
    io.feedback.input_index=3;
    io.feedback.buffer=(ctx->inputs && ctx->input_count > (3)) ? ctx->inputs[3] : NULL;
    if (!valid_buffer(io.feedback.buffer, (uint32_t)(s->channels),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.feedback.samples=io.feedback.buffer ? (const float*)io.feedback.buffer->data : NULL;
    if (io.feedback.buffer && io.feedback.buffer->frame_count != (uint32_t)_orpheus_payload_feedback)
        return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_payload_start=(double)(1);
    if (!isfinite(_orpheus_payload_start) || _orpheus_payload_start < 1
            || _orpheus_payload_start > UINT32_MAX || _orpheus_payload_start != (uint32_t)_orpheus_payload_start)
        return ORPHEUS_ERR_INVALID_ARG;
    io.start.active=true;
    io.start.drive_port_no=1u;
    io.start.buffer=(ctx->outputs && ctx->output_count > (0)) ? ctx->outputs[0] : NULL;
    if (!valid_buffer(io.start.buffer, (uint32_t)(s->channels),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.start.samples=io.start.buffer ? (float*)io.start.buffer->data : NULL;
    if (io.start.buffer) {
        if ((uint32_t)_orpheus_payload_start > io.start.buffer->frame_capacity) return ORPHEUS_ERR_INVALID_ARG;
        io.start.buffer->frame_count=(uint32_t)_orpheus_payload_start;
    }
    io.start.delivery=(OrpheusDelivery){.buffer=io.start.buffer, .frame_index=ctx->frame_index,
        .epoch=ctx->epoch, .timeline_flags=ctx->timeline_flags};
    double _orpheus_payload_accepted=(double)(1);
    if (!isfinite(_orpheus_payload_accepted) || _orpheus_payload_accepted < 1
            || _orpheus_payload_accepted > UINT32_MAX || _orpheus_payload_accepted != (uint32_t)_orpheus_payload_accepted)
        return ORPHEUS_ERR_INVALID_ARG;
    io.accepted.active=true;
    io.accepted.drive_port_no=2u;
    io.accepted.buffer=(ctx->outputs && ctx->output_count > (1)) ? ctx->outputs[1] : NULL;
    if (!valid_buffer(io.accepted.buffer, (uint32_t)(s->channels),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.accepted.samples=io.accepted.buffer ? (float*)io.accepted.buffer->data : NULL;
    if (io.accepted.buffer) {
        if ((uint32_t)_orpheus_payload_accepted > io.accepted.buffer->frame_capacity) return ORPHEUS_ERR_INVALID_ARG;
        io.accepted.buffer->frame_count=(uint32_t)_orpheus_payload_accepted;
    }
    io.accepted.delivery=(OrpheusDelivery){.buffer=io.accepted.buffer, .frame_index=ctx->frame_index,
        .epoch=ctx->epoch, .timeline_flags=ctx->timeline_flags};
    double _orpheus_payload_alpha=(double)(1);
    if (!isfinite(_orpheus_payload_alpha) || _orpheus_payload_alpha < 1
            || _orpheus_payload_alpha > UINT32_MAX || _orpheus_payload_alpha != (uint32_t)_orpheus_payload_alpha)
        return ORPHEUS_ERR_INVALID_ARG;
    io.alpha.active=true;
    io.alpha.drive_port_no=3u;
    io.alpha.buffer=(ctx->outputs && ctx->output_count > (2)) ? ctx->outputs[2] : NULL;
    if (!valid_buffer(io.alpha.buffer, (uint32_t)(1),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.alpha.samples=io.alpha.buffer ? (float*)io.alpha.buffer->data : NULL;
    if (io.alpha.buffer) {
        if ((uint32_t)_orpheus_payload_alpha > io.alpha.buffer->frame_capacity) return ORPHEUS_ERR_INVALID_ARG;
        io.alpha.buffer->frame_count=(uint32_t)_orpheus_payload_alpha;
    }
    io.alpha.delivery=(OrpheusDelivery){.buffer=io.alpha.buffer, .frame_index=ctx->frame_index,
        .epoch=ctx->epoch, .timeline_flags=ctx->timeline_flags};
    double _orpheus_payload_freeze=(double)(1);
    if (!isfinite(_orpheus_payload_freeze) || _orpheus_payload_freeze < 1
            || _orpheus_payload_freeze > UINT32_MAX || _orpheus_payload_freeze != (uint32_t)_orpheus_payload_freeze)
        return ORPHEUS_ERR_INVALID_ARG;
    io.freeze.active=true;
    io.freeze.drive_port_no=4u;
    io.freeze.buffer=(ctx->outputs && ctx->output_count > (3)) ? ctx->outputs[3] : NULL;
    if (!valid_buffer(io.freeze.buffer, (uint32_t)(1),
        ORPHEUS_FORMAT_I32, sizeof(int32_t), _Alignof(int32_t)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.freeze.samples=io.freeze.buffer ? (int32_t*)io.freeze.buffer->data : NULL;
    if (io.freeze.buffer) {
        if ((uint32_t)_orpheus_payload_freeze > io.freeze.buffer->frame_capacity) return ORPHEUS_ERR_INVALID_ARG;
        io.freeze.buffer->frame_count=(uint32_t)_orpheus_payload_freeze;
    }
    io.freeze.delivery=(OrpheusDelivery){.buffer=io.freeze.buffer, .frame_index=ctx->frame_index,
        .epoch=ctx->epoch, .timeline_flags=ctx->timeline_flags};
    return orpheus_builtin_sequence_envelope_state_algorithm(s, &io, ctx);
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
