#include "component_state.h"
#include <string.h>
#include <math.h>

static const OrpheusPort ports[] = {
    {.id="in", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
};

static const OrpheusParameter params[] = {
    {.id="channels", .name="通道数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=2}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=2, .max_i32=10},
    {.id="fft_size", .name="FFT点数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=128}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=4, .max_i32=4096},
    {.id="smoothing", .name="平滑块数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=8}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_i32=1, .max_i32=128},
    {.id="coherence", .name="相干矩阵", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str="{}"}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="history", .name="相干历史", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str="[]"}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
};

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.coherence_matrix", .version="1.0.0", .abi_version=7,
    .ports=ports, .port_count=2,
    .params=params, .param_count=5,
    .state_size=sizeof(orpheus_builtin_coherence_matrix_State), .alignment=_Alignof(orpheus_builtin_coherence_matrix_State),
    .scratch_size=0, .latency_samples=0,
    .realtime_safe=true, .supports_inplace=true
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

static void snapshot_parameters(const orpheus_builtin_coherence_matrix_State* s, orpheus_builtin_coherence_matrix_Parameters* p) {
    (void)s; memset(p, 0, sizeof(*p));
    p->channels=s->channels;
    p->fft_size=s->fft_size;
    p->smoothing=s->smoothing;
    memcpy(p->coherence, s->coherence, sizeof(p->coherence));
    memcpy(p->history, s->history, sizeof(p->history));
}
static void commit_parameters(orpheus_builtin_coherence_matrix_State* s, const orpheus_builtin_coherence_matrix_Parameters* p) {
    (void)s; (void)p;
    s->channels=p->channels;
    s->fft_size=p->fft_size;
    s->smoothing=p->smoothing;
    memcpy(s->coherence, p->coherence, sizeof(s->coherence));
    memcpy(s->history, p->history, sizeof(s->history));
}
static void default_parameters(orpheus_builtin_coherence_matrix_Parameters* p) {
    memset(p, 0, sizeof(*p));
    p->channels=2;
    p->fft_size=128;
    p->smoothing=8;
    memcpy(p->coherence, "{}", 3);
    memcpy(p->history, "[]", 3);
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
static int update_parameter(orpheus_builtin_coherence_matrix_Parameters* p, const char* key, const OrpheusValue* value) {
    if (!p || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "channels") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<2 || value->value.i32>10) return ORPHEUS_ERR_INVALID_ARG;
        p->channels=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "fft_size") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<4 || value->value.i32>4096) return ORPHEUS_ERR_INVALID_ARG;
        p->fft_size=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "smoothing") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>128) return ORPHEUS_ERR_INVALID_ARG;
        p->smoothing=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "coherence") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->coherence), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->coherence)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->coherence, text, sizeof(text));
        return ORPHEUS_OK;
    }
    if (strcmp(key, "history") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->history), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->history)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->history, text, sizeof(text));
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int normalize_parameters(orpheus_builtin_coherence_matrix_Parameters* p, const char* key) {
    int result=orpheus_builtin_coherence_matrix_normalize(p, key);
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
        OrpheusValue value={.type=ORPHEUS_VALUE_INT,.value.i32=p->smoothing};
        result=update_parameter(p, "smoothing", &value);
        if (result!=ORPHEUS_OK) return result;
    }
    {
        OrpheusValue value={.type=ORPHEUS_VALUE_STRING,.value.str=p->coherence};
        result=update_parameter(p, "coherence", &value);
        if (result!=ORPHEUS_OK) return result;
    }
    {
        OrpheusValue value={.type=ORPHEUS_VALUE_STRING,.value.str=p->history};
        result=update_parameter(p, "history", &value);
        if (result!=ORPHEUS_OK) return result;
    }
    return ORPHEUS_OK;
}
static int parameter_value(const orpheus_builtin_coherence_matrix_Parameters* p, const char* key, OrpheusValue* value) {
    (void)p; (void)key; (void)value;
    if (strcmp(key, "channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->channels; return ORPHEUS_OK;
    }
    if (strcmp(key, "fft_size")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->fft_size; return ORPHEUS_OK;
    }
    if (strcmp(key, "smoothing")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=p->smoothing; return ORPHEUS_OK;
    }
    if (strcmp(key, "coherence")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=p->coherence; return ORPHEUS_OK;
    }
    if (strcmp(key, "history")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=p->history; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int set_parameter(void* state, const char* key, const OrpheusValue* value) {
    orpheus_builtin_coherence_matrix_State* s=(orpheus_builtin_coherence_matrix_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "coherence")==0) return ORPHEUS_ERR_UNSUPPORTED;
    if (strcmp(key, "history")==0) return ORPHEUS_ERR_UNSUPPORTED;
    orpheus_builtin_coherence_matrix_Parameters before, candidate; snapshot_parameters(s, &before); candidate=before;
    int result=update_parameter(&candidate, key, value);
    if (result!=ORPHEUS_OK) return result;
    result=normalize_parameters(&candidate, key); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate); return ORPHEUS_OK;
}
static int get_parameter(void* state, const char* key, OrpheusValue* value) {
    orpheus_builtin_coherence_matrix_State* s=(orpheus_builtin_coherence_matrix_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_coherence_matrix_Parameters before, checked; snapshot_parameters(s, &before); checked=before;
    OrpheusValue candidate={0};
    int result=orpheus_builtin_coherence_matrix_get_parameter(s, key, &candidate);
    if (result==ORPHEUS_OK) result=update_parameter(&checked, key, &candidate);
    commit_parameters(s, &before);
    if (result!=ORPHEUS_ERR_NOT_FOUND) {
        if (result!=ORPHEUS_OK) return result;
        result=parameter_value(&checked, key, value);
        if (result!=ORPHEUS_OK) return result;
        if (strcmp(key, "coherence")==0) {
            memcpy(s->_orpheus_readback_coherence, checked.coherence, sizeof(checked.coherence));
            value->value.str=s->_orpheus_readback_coherence;
        }
        if (strcmp(key, "history")==0) {
            memcpy(s->_orpheus_readback_history, checked.history, sizeof(checked.history));
            value->value.str=s->_orpheus_readback_history;
        }
        return ORPHEUS_OK;
    }
    if (strcmp(key, "channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->channels; return ORPHEUS_OK;
    }
    if (strcmp(key, "fft_size")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->fft_size; return ORPHEUS_OK;
    }
    if (strcmp(key, "smoothing")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->smoothing; return ORPHEUS_OK;
    }
    if (strcmp(key, "coherence")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->coherence; return ORPHEUS_OK;
    }
    if (strcmp(key, "history")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->history; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int configure_parameters(orpheus_builtin_coherence_matrix_Parameters* p, const OrpheusConfig* config) {
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
static int cleanup(orpheus_builtin_coherence_matrix_State* s) {
    int result=ORPHEUS_OK;
    if (s->_orpheus_cleanup_pending) {
        s->_orpheus_cleanup_pending=false;
        result=orpheus_builtin_coherence_matrix_destroy(s);
        memset(&s->algorithm, 0, sizeof(s->algorithm));
    }
    return result;
}
static int prepare(void* state, const OrpheusConfig* config) {
    orpheus_builtin_coherence_matrix_State* s=(orpheus_builtin_coherence_matrix_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_coherence_matrix_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    result=cleanup(s); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate);
    s->_orpheus_cleanup_pending=true;
    result=orpheus_builtin_coherence_matrix_prepare(s, config);
    if (result!=ORPHEUS_OK) { (void)cleanup(s); return result; }
    return ORPHEUS_OK;
}
static int create(void** state, const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=NULL;
    if (!config || !config->state_block) return ORPHEUS_ERR_INVALID_ARG;
    if ((uintptr_t)config->state_block % descriptor.alignment) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_coherence_matrix_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    orpheus_builtin_coherence_matrix_State* s=(orpheus_builtin_coherence_matrix_State*)config->state_block;
    memset(s, 0, sizeof(*s)); commit_parameters(s, &candidate);
    s->_orpheus_alive=true; *state=s; return ORPHEUS_OK;
}
static int destroy(void* state) {
    orpheus_builtin_coherence_matrix_State* s=(orpheus_builtin_coherence_matrix_State*)state;
    if (!s) return ORPHEUS_ERR_INVALID_ARG;
    if (!s->_orpheus_alive) return ORPHEUS_OK;
    int result=cleanup(s); s->_orpheus_alive=false; return result;
}
static int reset(void* state) {
    orpheus_builtin_coherence_matrix_State* s=(orpheus_builtin_coherence_matrix_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_builtin_coherence_matrix_reset(s);
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    orpheus_builtin_coherence_matrix_State* s=(orpheus_builtin_coherence_matrix_State*)state;
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=2, .max_i32=10)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, fft_size, ORPHEUS_SLOT_SETTING, "fft_size", "FFT点数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=4, .max_i32=4096)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, smoothing, ORPHEUS_SLOT_SETTING, "smoothing", "平滑块数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_i32=1, .max_i32=128)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, coherence, ORPHEUS_SLOT_PROBE, "coherence", "相干矩阵", ORPHEUS_VALUE_STRING, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, history, ORPHEUS_SLOT_PROBE, "history", "相干历史", ORPHEUS_VALUE_STRING, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
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
    orpheus_builtin_coherence_matrix_State* s=(orpheus_builtin_coherence_matrix_State*)state;
    orpheus_builtin_coherence_matrix_IO io={0};
    if (!s || !s->_orpheus_alive || !ctx) return ORPHEUS_ERR_INVALID_ARG;
    double _orpheus_payload_in=(double)(s->fft_size);
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
    double _orpheus_payload_out=(double)(s->fft_size);
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
    return orpheus_builtin_coherence_matrix_algorithm(s, &io, ctx);
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
