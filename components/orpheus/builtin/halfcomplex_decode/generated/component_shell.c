#include "component_state.h"
#include <string.h>
#include <math.h>

static const OrpheusPort ports[] = {
    {.id="in", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=((double)(0) * (double)(0)), .is_variable=true, .channels_param="channels"},
    {.id="spectrum", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_BULK, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=((double)(0) * (double)(0)), .is_variable=true, .channels_param="channels"},
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_BULK, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=((double)(0) * (double)(2) * (double)(0)), .is_variable=true, .channels_param="channels"},
    {.id="tick", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_BULK, .sample_format=ORPHEUS_FORMAT_I32, .channels=1, .sample_rate=0, .block_size=1, .is_variable=false, .channels_param=NULL},
};

static const OrpheusParameter params[] = {
    {.id="channels", .name="通道数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=2}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=1, .max_i32=64},
    {.id="fft_size", .name="每帧半复数槽数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=256}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=2, .max_i32=65536},
    {.id="batches", .name="批次数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=1}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=1, .max_i32=64},
    {.id="first_bin", .name="首频点（从0起）", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=0}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=0, .max_i32=32768},
    {.id="bins", .name="输出频点数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=129}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=1, .max_i32=32769},
};

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.halfcomplex_decode", .version="1.1.0", .abi_version=7,
    .ports=ports, .port_count=4,
    .params=params, .param_count=5,
    .state_size=sizeof(orpheus_builtin_halfcomplex_decode_State), .alignment=_Alignof(orpheus_builtin_halfcomplex_decode_State),
    .scratch_size=0, .latency_samples=0,
    .realtime_safe=true, .supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

static void snapshot_parameters(const orpheus_builtin_halfcomplex_decode_State* s, orpheus_builtin_halfcomplex_decode_Parameters* p) {
    (void)s; memset(p, 0, sizeof(*p));
    p->channels=s->channels;
    p->fft_size=s->fft_size;
    p->batches=s->batches;
    p->first_bin=s->first_bin;
    p->bins=s->bins;
}
static void commit_parameters(orpheus_builtin_halfcomplex_decode_State* s, const orpheus_builtin_halfcomplex_decode_Parameters* p) {
    (void)s; (void)p;
    s->channels=p->channels;
    s->fft_size=p->fft_size;
    s->batches=p->batches;
    s->first_bin=p->first_bin;
    s->bins=p->bins;
}
static void default_parameters(orpheus_builtin_halfcomplex_decode_Parameters* p) {
    memset(p, 0, sizeof(*p));
    p->channels=2;
    p->fft_size=256;
    p->batches=1;
    p->first_bin=0;
    p->bins=129;
}
static int update_parameter(orpheus_builtin_halfcomplex_decode_Parameters* p, const char* key, const OrpheusValue* value) {
    if (!p || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "channels") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>64) return ORPHEUS_ERR_INVALID_ARG;
        p->channels=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "fft_size") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<2 || value->value.i32>65536) return ORPHEUS_ERR_INVALID_ARG;
        p->fft_size=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "batches") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>64) return ORPHEUS_ERR_INVALID_ARG;
        p->batches=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "first_bin") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<0 || value->value.i32>32768) return ORPHEUS_ERR_INVALID_ARG;
        p->first_bin=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "bins") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>32769) return ORPHEUS_ERR_INVALID_ARG;
        p->bins=value->value.i32;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int set_parameter(void* state, const char* key, const OrpheusValue* value) {
    orpheus_builtin_halfcomplex_decode_State* s=(orpheus_builtin_halfcomplex_decode_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_halfcomplex_decode_Parameters before, candidate; snapshot_parameters(s, &before); candidate=before;
    int result=update_parameter(&candidate, key, value);
    if (result!=ORPHEUS_OK) return result;
    result=orpheus_builtin_halfcomplex_decode_validate(&candidate); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate); return ORPHEUS_OK;
}
static int get_parameter(void* state, const char* key, OrpheusValue* value) {
    orpheus_builtin_halfcomplex_decode_State* s=(orpheus_builtin_halfcomplex_decode_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->channels; return ORPHEUS_OK;
    }
    if (strcmp(key, "fft_size")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->fft_size; return ORPHEUS_OK;
    }
    if (strcmp(key, "batches")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->batches; return ORPHEUS_OK;
    }
    if (strcmp(key, "first_bin")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->first_bin; return ORPHEUS_OK;
    }
    if (strcmp(key, "bins")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->bins; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int configure_parameters(orpheus_builtin_halfcomplex_decode_Parameters* p, const OrpheusConfig* config) {
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
    return orpheus_builtin_halfcomplex_decode_validate(p);
}
static int cleanup(orpheus_builtin_halfcomplex_decode_State* s) {
    int result=ORPHEUS_OK;
    if (s->_orpheus_cleanup_pending) {
        s->_orpheus_cleanup_pending=false;
        memset(&s->algorithm, 0, sizeof(s->algorithm));
    }
    return result;
}
static int prepare(void* state, const OrpheusConfig* config) {
    orpheus_builtin_halfcomplex_decode_State* s=(orpheus_builtin_halfcomplex_decode_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_halfcomplex_decode_Parameters candidate; int result=configure_parameters(&candidate, config);
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
    orpheus_builtin_halfcomplex_decode_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    orpheus_builtin_halfcomplex_decode_State* s=(orpheus_builtin_halfcomplex_decode_State*)config->state_block;
    memset(s, 0, sizeof(*s)); commit_parameters(s, &candidate);
    s->_orpheus_alive=true; *state=s; return ORPHEUS_OK;
}
static int destroy(void* state) {
    orpheus_builtin_halfcomplex_decode_State* s=(orpheus_builtin_halfcomplex_decode_State*)state;
    if (!s) return ORPHEUS_ERR_INVALID_ARG;
    if (!s->_orpheus_alive) return ORPHEUS_OK;
    int result=cleanup(s); s->_orpheus_alive=false; return result;
}
static int reset(void* state) {
    orpheus_builtin_halfcomplex_decode_State* s=(orpheus_builtin_halfcomplex_decode_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    memset(&s->algorithm, 0, sizeof(s->algorithm)); return ORPHEUS_OK;
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    orpheus_builtin_halfcomplex_decode_State* s=(orpheus_builtin_halfcomplex_decode_State*)state;
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=1, .max_i32=64)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, fft_size, ORPHEUS_SLOT_SETTING, "fft_size", "每帧半复数槽数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=2, .max_i32=65536)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, batches, ORPHEUS_SLOT_SETTING, "batches", "批次数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=1, .max_i32=64)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, first_bin, ORPHEUS_SLOT_SETTING, "first_bin", "首频点（从0起）", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=0, .max_i32=32768)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, bins, ORPHEUS_SLOT_SETTING, "bins", "输出频点数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=1, .max_i32=32769)==ORPHEUS_SLOT_ID_INVALID)
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
    orpheus_builtin_halfcomplex_decode_State* s=(orpheus_builtin_halfcomplex_decode_State*)state;
    orpheus_builtin_halfcomplex_decode_IO io={0};
    if (!s || !s->_orpheus_alive || !ctx) return ORPHEUS_ERR_INVALID_ARG;
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (s->fft_size < 2 || s->fft_size > 65536) return ORPHEUS_ERR_INVALID_ARG;
    if (s->batches < 1 || s->batches > 64) return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_payload_in=(double)(((double)(s->fft_size) * (double)(s->batches)));
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
    if (s->fft_size < 2 || s->fft_size > 65536) return ORPHEUS_ERR_INVALID_ARG;
    if (s->batches < 1 || s->batches > 64) return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_payload_spectrum=(double)(((double)(s->fft_size) * (double)(s->batches)));
    if (!isfinite(_orpheus_payload_spectrum) || _orpheus_payload_spectrum < 1
            || _orpheus_payload_spectrum > UINT32_MAX || _orpheus_payload_spectrum != (uint32_t)_orpheus_payload_spectrum)
        return ORPHEUS_ERR_INVALID_ARG;
    if (s->fft_size < 2 || s->fft_size > 65536) return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_frame_size_spectrum=(double)(s->fft_size);
    if (!isfinite(_orpheus_frame_size_spectrum) || _orpheus_frame_size_spectrum < 1 || _orpheus_frame_size_spectrum > UINT32_MAX
            || _orpheus_frame_size_spectrum != (uint32_t)_orpheus_frame_size_spectrum
            || (uint32_t)_orpheus_payload_spectrum % (uint32_t)_orpheus_frame_size_spectrum != 0)
        return ORPHEUS_ERR_INVALID_ARG;
    io.spectrum.input_index=1;
    io.spectrum.buffer=(ctx->inputs && ctx->input_count > (1)) ? ctx->inputs[1] : NULL;
    if (!valid_buffer(io.spectrum.buffer, (uint32_t)(s->channels),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.spectrum.samples=io.spectrum.buffer ? (const float*)io.spectrum.buffer->data : NULL;
    if (io.spectrum.buffer && io.spectrum.buffer->frame_count != (uint32_t)_orpheus_payload_spectrum)
        return ORPHEUS_ERR_INVALID_ARG;
    if (s->bins < 1 || s->bins > 32769) return ORPHEUS_ERR_INVALID_ARG;
    if (s->batches < 1 || s->batches > 64) return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_payload_out=(double)(((double)(s->bins) * (double)(2) * (double)(s->batches)));
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
    double _orpheus_payload_tick=(double)(1);
    if (!isfinite(_orpheus_payload_tick) || _orpheus_payload_tick < 1
            || _orpheus_payload_tick > UINT32_MAX || _orpheus_payload_tick != (uint32_t)_orpheus_payload_tick)
        return ORPHEUS_ERR_INVALID_ARG;
    io.tick.active=true;
    io.tick.drive_port_no=2u;
    io.tick.buffer=(ctx->outputs && ctx->output_count > (1)) ? ctx->outputs[1] : NULL;
    if (!valid_buffer(io.tick.buffer, (uint32_t)(1),
        ORPHEUS_FORMAT_I32, sizeof(int32_t), _Alignof(int32_t)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.tick.samples=io.tick.buffer ? (int32_t*)io.tick.buffer->data : NULL;
    if (io.tick.buffer) {
        if ((uint32_t)_orpheus_payload_tick > io.tick.buffer->frame_capacity) return ORPHEUS_ERR_INVALID_ARG;
        io.tick.buffer->frame_count=(uint32_t)_orpheus_payload_tick;
    }
    io.tick.delivery=(OrpheusDelivery){.buffer=io.tick.buffer, .frame_index=ctx->frame_index,
        .epoch=ctx->epoch, .timeline_flags=ctx->timeline_flags};
    if ((uint32_t)(io.in.buffer != NULL) + (uint32_t)(io.spectrum.buffer != NULL) != UINT32_C(1)) return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_builtin_halfcomplex_decode_algorithm(s, &io, ctx);
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
