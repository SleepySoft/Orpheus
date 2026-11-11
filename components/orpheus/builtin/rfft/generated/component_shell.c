#include "component_state.h"
#include <string.h>
#include <math.h>

static const OrpheusPort ports[] = {
    {.id="in", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=((double)(0) * (double)(0)), .is_variable=true, .channels_param="channels"},
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=((double)(0) * (double)(0)), .is_variable=true, .channels_param="channels"},
    {.id="spectrum", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_BULK, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=((double)(0) * (double)(0)), .is_variable=true, .channels_param="channels"},
};

static const OrpheusParameter params[] = {
    {.id="channels", .name="通道数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=2}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=1, .max_i32=32},
    {.id="fft_size", .name="每帧FFT点数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=256}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=4, .max_i32=1024},
    {.id="batches", .name="顺序批次数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=1}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=1, .max_i32=64},
    {.id="output_mode", .name="输出模式", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str="halfcomplex"}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
};

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.rfft", .version="1.2.0", .abi_version=7,
    .ports=ports, .port_count=3,
    .params=params, .param_count=4,
    .state_size=sizeof(orpheus_builtin_rfft_State), .alignment=_Alignof(orpheus_builtin_rfft_State),
    .scratch_size=0, .latency_samples=0,
    .realtime_safe=true, .supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

static void snapshot_parameters(const orpheus_builtin_rfft_State* s, orpheus_builtin_rfft_Parameters* p) {
    (void)s; memset(p, 0, sizeof(*p));
    p->channels=s->channels;
    p->fft_size=s->fft_size;
    p->batches=s->batches;
    memcpy(p->output_mode, s->output_mode, sizeof(p->output_mode));
}
static void commit_parameters(orpheus_builtin_rfft_State* s, const orpheus_builtin_rfft_Parameters* p) {
    (void)s; (void)p;
    s->channels=p->channels;
    s->fft_size=p->fft_size;
    s->batches=p->batches;
    memcpy(s->output_mode, p->output_mode, sizeof(s->output_mode));
}
static void default_parameters(orpheus_builtin_rfft_Parameters* p) {
    memset(p, 0, sizeof(*p));
    p->channels=2;
    p->fft_size=256;
    p->batches=1;
    memcpy(p->output_mode, "halfcomplex", 12);
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
static int update_parameter(orpheus_builtin_rfft_Parameters* p, const char* key, const OrpheusValue* value) {
    if (!p || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "channels") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>32) return ORPHEUS_ERR_INVALID_ARG;
        p->channels=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "fft_size") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<4 || value->value.i32>1024) return ORPHEUS_ERR_INVALID_ARG;
        p->fft_size=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "batches") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>64) return ORPHEUS_ERR_INVALID_ARG;
        p->batches=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "output_mode") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->output_mode), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->output_mode)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->output_mode, text, sizeof(text));
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int normalize_parameters(orpheus_builtin_rfft_Parameters* p, const char* key) {
    int result=orpheus_builtin_rfft_normalize(p, key);
    if (result!=ORPHEUS_OK) return result;
    {
        OrpheusValue value={.type=ORPHEUS_VALUE_INT,.value.i32=p->channels};
        result=update_parameter(p, "channels", &value);
        if (result!=ORPHEUS_OK) return result;
    }
    {
        OrpheusValue value={.type=ORPHEUS_VALUE_INT,.value.i32=p->fft_size};
        result=update_parameter(p, "fft_size", &value);
        if (result!=ORPHEUS_OK) return result;
    }
    {
        OrpheusValue value={.type=ORPHEUS_VALUE_INT,.value.i32=p->batches};
        result=update_parameter(p, "batches", &value);
        if (result!=ORPHEUS_OK) return result;
    }
    {
        OrpheusValue value={.type=ORPHEUS_VALUE_STRING,.value.str=p->output_mode};
        result=update_parameter(p, "output_mode", &value);
        if (result!=ORPHEUS_OK) return result;
    }
    return ORPHEUS_OK;
}
static int set_parameter(void* state, const char* key, const OrpheusValue* value) {
    orpheus_builtin_rfft_State* s=(orpheus_builtin_rfft_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_rfft_Parameters before, candidate; snapshot_parameters(s, &before); candidate=before;
    int result=update_parameter(&candidate, key, value);
    if (result!=ORPHEUS_OK) return result;
    result=normalize_parameters(&candidate, key); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate); return ORPHEUS_OK;
}
static int get_parameter(void* state, const char* key, OrpheusValue* value) {
    orpheus_builtin_rfft_State* s=(orpheus_builtin_rfft_State*)state;
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
    if (strcmp(key, "output_mode")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->output_mode; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int configure_parameters(orpheus_builtin_rfft_Parameters* p, const OrpheusConfig* config) {
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
static int cleanup(orpheus_builtin_rfft_State* s) {
    int result=ORPHEUS_OK;
    if (s->_orpheus_cleanup_pending) {
        s->_orpheus_cleanup_pending=false;
        memset(&s->algorithm, 0, sizeof(s->algorithm));
    }
    return result;
}
static int prepare(void* state, const OrpheusConfig* config) {
    orpheus_builtin_rfft_State* s=(orpheus_builtin_rfft_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_rfft_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    result=cleanup(s); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate);
    s->_orpheus_cleanup_pending=true;
    result=orpheus_builtin_rfft_prepare(s, config);
    if (result!=ORPHEUS_OK) { (void)cleanup(s); return result; }
    return ORPHEUS_OK;
}
static int create(void** state, const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=NULL;
    if (!config || !config->state_block) return ORPHEUS_ERR_INVALID_ARG;
    if ((uintptr_t)config->state_block % descriptor.alignment) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_rfft_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    orpheus_builtin_rfft_State* s=(orpheus_builtin_rfft_State*)config->state_block;
    memset(s, 0, sizeof(*s)); commit_parameters(s, &candidate);
    s->_orpheus_alive=true; *state=s; return ORPHEUS_OK;
}
static int destroy(void* state) {
    orpheus_builtin_rfft_State* s=(orpheus_builtin_rfft_State*)state;
    if (!s) return ORPHEUS_ERR_INVALID_ARG;
    if (!s->_orpheus_alive) return ORPHEUS_OK;
    int result=cleanup(s); s->_orpheus_alive=false; return result;
}
static int reset(void* state) {
    orpheus_builtin_rfft_State* s=(orpheus_builtin_rfft_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    memset(&s->algorithm, 0, sizeof(s->algorithm)); return ORPHEUS_OK;
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    orpheus_builtin_rfft_State* s=(orpheus_builtin_rfft_State*)state;
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=1, .max_i32=32)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, fft_size, ORPHEUS_SLOT_SETTING, "fft_size", "每帧FFT点数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=4, .max_i32=1024)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, batches, ORPHEUS_SLOT_SETTING, "batches", "顺序批次数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=1, .max_i32=64)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, output_mode, ORPHEUS_SLOT_SETTING, "output_mode", "输出模式", ORPHEUS_VALUE_STRING, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
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
    orpheus_builtin_rfft_State* s=(orpheus_builtin_rfft_State*)state;
    orpheus_builtin_rfft_IO io={0};
    if (!s || !s->_orpheus_alive || !ctx) return ORPHEUS_ERR_INVALID_ARG;
    if (s->fft_size < 4 || s->fft_size > 1024) return ORPHEUS_ERR_INVALID_ARG;
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
    if (s->fft_size < 4 || s->fft_size > 1024) return ORPHEUS_ERR_INVALID_ARG;
    if (s->batches < 1 || s->batches > 64) return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_payload_out=(double)(((double)(s->fft_size) * (double)(s->batches)));
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
    if (s->fft_size < 4 || s->fft_size > 1024) return ORPHEUS_ERR_INVALID_ARG;
    if (s->batches < 1 || s->batches > 64) return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_payload_spectrum=(double)(((double)(s->fft_size) * (double)(s->batches)));
    if (!isfinite(_orpheus_payload_spectrum) || _orpheus_payload_spectrum < 1
            || _orpheus_payload_spectrum > UINT32_MAX || _orpheus_payload_spectrum != (uint32_t)_orpheus_payload_spectrum)
        return ORPHEUS_ERR_INVALID_ARG;
    if (s->fft_size < 4 || s->fft_size > 1024) return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_frame_size_spectrum=(double)(s->fft_size);
    if (!isfinite(_orpheus_frame_size_spectrum) || _orpheus_frame_size_spectrum < 1 || _orpheus_frame_size_spectrum > UINT32_MAX
            || _orpheus_frame_size_spectrum != (uint32_t)_orpheus_frame_size_spectrum
            || (uint32_t)_orpheus_payload_spectrum % (uint32_t)_orpheus_frame_size_spectrum != 0)
        return ORPHEUS_ERR_INVALID_ARG;
    io.spectrum.active=true;
    io.spectrum.drive_port_no=2u;
    io.spectrum.buffer=(ctx->outputs && ctx->output_count > (1)) ? ctx->outputs[1] : NULL;
    if (!valid_buffer(io.spectrum.buffer, (uint32_t)(s->channels),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.spectrum.samples=io.spectrum.buffer ? (float*)io.spectrum.buffer->data : NULL;
    if (io.spectrum.buffer) {
        if ((uint32_t)_orpheus_payload_spectrum > io.spectrum.buffer->frame_capacity) return ORPHEUS_ERR_INVALID_ARG;
        io.spectrum.buffer->frame_count=(uint32_t)_orpheus_payload_spectrum;
    }
    io.spectrum.delivery=(OrpheusDelivery){.buffer=io.spectrum.buffer, .frame_index=ctx->frame_index,
        .epoch=ctx->epoch, .timeline_flags=ctx->timeline_flags};
    return orpheus_builtin_rfft_algorithm(s, &io, ctx);
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
