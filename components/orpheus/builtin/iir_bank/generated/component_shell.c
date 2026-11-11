#include "component_state.h"
#include <string.h>
#include <math.h>

_Static_assert(_Generic(&(((orpheus_builtin_iir_bank_State*)0)->algorithm.shared), float (*)[80]: 1, default: 0),
    "BULK coefs: storage 类型或固定容量不匹配");
_Static_assert(_Generic(&(((orpheus_builtin_iir_bank_State*)0)->algorithm.per_channel), float (*)[5120]: 1, default: 0),
    "BULK initial_coefs_per_channel: storage 类型或固定容量不匹配");
static const OrpheusPort ports[] = {
    {.id="in", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels"},
};

static const OrpheusParameter params[] = {
    {.id="channels", .name="通道数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=2}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=1, .max_i32=64},
    {.id="num_stages", .name="级数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=4}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_i32=1, .max_i32=16},
    {.id="coefs_mode", .name="系数模式", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str="shared"}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true},
    {.id="coefs", .name="初始系数（未提供部分保持单位节）", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str="1,0,0,0,0"}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=false, .persistent=true, .affects_signature=false},
    {.id="initial_coefs_per_channel", .name="逐通道系数初值（空值使用coefs）", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str=""}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=false, .persistent=true, .affects_signature=false},
    {.id="processing_enabled", .name="处理使能（0输出正零并冻结历史）", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=1}, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_i32=0, .max_i32=1},
    {.id="stages_per_channel", .name="每通道实际级数（空值沿用统一级数）", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str=""}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
};

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.iir_bank", .version="1.5.0", .abi_version=7,
    .ports=ports, .port_count=2,
    .params=params, .param_count=7,
    .state_size=sizeof(orpheus_builtin_iir_bank_State), .alignment=_Alignof(orpheus_builtin_iir_bank_State),
    .scratch_size=0, .latency_samples=0,
    .realtime_safe=true, .supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

static void snapshot_parameters(const orpheus_builtin_iir_bank_State* s, orpheus_builtin_iir_bank_Parameters* p) {
    (void)s; memset(p, 0, sizeof(*p));
    p->channels=s->channels;
    p->num_stages=s->num_stages;
    memcpy(p->coefs_mode, s->coefs_mode, sizeof(p->coefs_mode));
    memcpy(p->coefs, s->coefs, sizeof(p->coefs));
    memcpy(p->initial_coefs_per_channel, s->initial_coefs_per_channel, sizeof(p->initial_coefs_per_channel));
    p->processing_enabled=s->processing_enabled;
    memcpy(p->stages_per_channel, s->stages_per_channel, sizeof(p->stages_per_channel));
}
static void commit_parameters(orpheus_builtin_iir_bank_State* s, const orpheus_builtin_iir_bank_Parameters* p) {
    (void)s; (void)p;
    s->channels=p->channels;
    s->num_stages=p->num_stages;
    memcpy(s->coefs_mode, p->coefs_mode, sizeof(s->coefs_mode));
    memcpy(s->coefs, p->coefs, sizeof(s->coefs));
    memcpy(s->initial_coefs_per_channel, p->initial_coefs_per_channel, sizeof(s->initial_coefs_per_channel));
    s->processing_enabled=p->processing_enabled;
    memcpy(s->stages_per_channel, p->stages_per_channel, sizeof(s->stages_per_channel));
}
static void default_parameters(orpheus_builtin_iir_bank_Parameters* p) {
    memset(p, 0, sizeof(*p));
    p->channels=2;
    p->num_stages=4;
    memcpy(p->coefs_mode, "shared", 7);
    memcpy(p->coefs, "1,0,0,0,0", 10);
    memcpy(p->initial_coefs_per_channel, "", 1);
    p->processing_enabled=1;
    memcpy(p->stages_per_channel, "", 1);
}
static int resource_count_coefs(const orpheus_builtin_iir_bank_Parameters* p, uint32_t* count) {
    (void)p; uint32_t product=1;
    int64_t dimension0=80;
    if (dimension0<1 || (uint64_t)dimension0>80u/product) return ORPHEUS_ERR_INVALID_ARG;
    product*=(uint32_t)dimension0;
    *count=product; return ORPHEUS_OK;
}
static int resource_count_initial_coefs_per_channel(const orpheus_builtin_iir_bank_Parameters* p, uint32_t* count) {
    (void)p; uint32_t product=1;
    int64_t dimension0=p->channels;
    if (dimension0<1 || (uint64_t)dimension0>5120u/product) return ORPHEUS_ERR_INVALID_ARG;
    product*=(uint32_t)dimension0;
    int64_t dimension1=16;
    if (dimension1<1 || (uint64_t)dimension1>5120u/product) return ORPHEUS_ERR_INVALID_ARG;
    product*=(uint32_t)dimension1;
    int64_t dimension2=5;
    if (dimension2<1 || (uint64_t)dimension2>5120u/product) return ORPHEUS_ERR_INVALID_ARG;
    product*=(uint32_t)dimension2;
    *count=product; return ORPHEUS_OK;
}
static int validate_resource_counts(const orpheus_builtin_iir_bank_Parameters* p) {
    uint32_t count; int result;
    result=resource_count_coefs(p, &count);
    if (result!=ORPHEUS_OK) return result;
    result=resource_count_initial_coefs_per_channel(p, &count);
    if (result!=ORPHEUS_OK) return result;
    return ORPHEUS_OK;
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
static int update_parameter(orpheus_builtin_iir_bank_Parameters* p, const char* key, const OrpheusValue* value) {
    if (!p || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "channels") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>64) return ORPHEUS_ERR_INVALID_ARG;
        p->channels=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "num_stages") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>16) return ORPHEUS_ERR_INVALID_ARG;
        p->num_stages=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "coefs_mode") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->coefs_mode), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->coefs_mode)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->coefs_mode, text, sizeof(text));
        return ORPHEUS_OK;
    }
    if (strcmp(key, "coefs") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->coefs), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->coefs)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->coefs, text, sizeof(text));
        return ORPHEUS_OK;
    }
    if (strcmp(key, "initial_coefs_per_channel") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->initial_coefs_per_channel), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->initial_coefs_per_channel)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->initial_coefs_per_channel, text, sizeof(text));
        return ORPHEUS_OK;
    }
    if (strcmp(key, "processing_enabled") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<0 || value->value.i32>1) return ORPHEUS_ERR_INVALID_ARG;
        p->processing_enabled=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "stages_per_channel") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->stages_per_channel), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->stages_per_channel)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->stages_per_channel, text, sizeof(text));
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int set_parameter(void* state, const char* key, const OrpheusValue* value) {
    orpheus_builtin_iir_bank_State* s=(orpheus_builtin_iir_bank_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_iir_bank_Parameters before, candidate; snapshot_parameters(s, &before); candidate=before;
    int result=update_parameter(&candidate, key, value);
    if (result!=ORPHEUS_OK) return result;
    result=validate_resource_counts(&candidate); if (result!=ORPHEUS_OK) return result;
    result=orpheus_builtin_iir_bank_validate(&candidate); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate); return ORPHEUS_OK;
}
static int get_parameter(void* state, const char* key, OrpheusValue* value) {
    orpheus_builtin_iir_bank_State* s=(orpheus_builtin_iir_bank_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->channels; return ORPHEUS_OK;
    }
    if (strcmp(key, "num_stages")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->num_stages; return ORPHEUS_OK;
    }
    if (strcmp(key, "coefs_mode")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->coefs_mode; return ORPHEUS_OK;
    }
    if (strcmp(key, "coefs")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->coefs; return ORPHEUS_OK;
    }
    if (strcmp(key, "initial_coefs_per_channel")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->initial_coefs_per_channel; return ORPHEUS_OK;
    }
    if (strcmp(key, "processing_enabled")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->processing_enabled; return ORPHEUS_OK;
    }
    if (strcmp(key, "stages_per_channel")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->stages_per_channel; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int configure_parameters(orpheus_builtin_iir_bank_Parameters* p, const OrpheusConfig* config) {
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
    int result=validate_resource_counts(p); if (result!=ORPHEUS_OK) return result;
    return orpheus_builtin_iir_bank_validate(p);
}
static int cleanup(orpheus_builtin_iir_bank_State* s) {
    int result=ORPHEUS_OK;
    if (s->_orpheus_cleanup_pending) {
        s->_orpheus_cleanup_pending=false;
        memset(&s->algorithm, 0, sizeof(s->algorithm));
    }
    return result;
}
static int prepare(void* state, const OrpheusConfig* config) {
    orpheus_builtin_iir_bank_State* s=(orpheus_builtin_iir_bank_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_iir_bank_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    result=cleanup(s); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate);
    s->_orpheus_cleanup_pending=true;
    result=orpheus_builtin_iir_bank_prepare(s, config);
    if (result!=ORPHEUS_OK) { (void)cleanup(s); return result; }
    return ORPHEUS_OK;
}
static int create(void** state, const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=NULL;
    if (!config || !config->state_block) return ORPHEUS_ERR_INVALID_ARG;
    if ((uintptr_t)config->state_block % descriptor.alignment) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_iir_bank_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    orpheus_builtin_iir_bank_State* s=(orpheus_builtin_iir_bank_State*)config->state_block;
    memset(s, 0, sizeof(*s)); commit_parameters(s, &candidate);
    s->_orpheus_alive=true; *state=s; return ORPHEUS_OK;
}
static int destroy(void* state) {
    orpheus_builtin_iir_bank_State* s=(orpheus_builtin_iir_bank_State*)state;
    if (!s) return ORPHEUS_ERR_INVALID_ARG;
    if (!s->_orpheus_alive) return ORPHEUS_OK;
    int result=cleanup(s); s->_orpheus_alive=false; return result;
}
static int reset(void* state) {
    orpheus_builtin_iir_bank_State* s=(orpheus_builtin_iir_bank_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_builtin_iir_bank_reset(s);
}
int orpheus_builtin_iir_bank_register_coefs(orpheus_builtin_iir_bank_State* s, const OrpheusRegistry* reg) {
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_iir_bank_Parameters current; snapshot_parameters(s, &current);
    uint32_t count_coefs;
    if (resource_count_coefs(&current, &count_coefs)!=ORPHEUS_OK) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_ARRAY(reg, s, algorithm.shared, count_coefs, ORPHEUS_SLOT_BULK, "coefs", "初始系数（未提供部分保持单位节）", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_DOUBLE_BUFFERED)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    return ORPHEUS_OK;
}
int orpheus_builtin_iir_bank_register_initial_coefs_per_channel(orpheus_builtin_iir_bank_State* s, const OrpheusRegistry* reg) {
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_iir_bank_Parameters current; snapshot_parameters(s, &current);
    uint32_t count_initial_coefs_per_channel;
    if (resource_count_initial_coefs_per_channel(&current, &count_initial_coefs_per_channel)!=ORPHEUS_OK) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_ARRAY(reg, s, algorithm.per_channel, count_initial_coefs_per_channel, ORPHEUS_SLOT_BULK, "coefs_per_channel", "逐通道系数初值（空值使用coefs）", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_DOUBLE_BUFFERED)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    return ORPHEUS_OK;
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    orpheus_builtin_iir_bank_State* s=(orpheus_builtin_iir_bank_State*)state;
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=1, .max_i32=64)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, num_stages, ORPHEUS_SLOT_SETTING, "num_stages", "级数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_i32=1, .max_i32=16)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, coefs_mode, ORPHEUS_SLOT_SETTING, "coefs_mode", "系数模式", ORPHEUS_VALUE_STRING, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, processing_enabled, ORPHEUS_SLOT_SETTING, "processing_enabled", "处理使能（0输出正零并冻结历史）", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_IMMEDIATE, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_i32=0, .max_i32=1)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, stages_per_channel, ORPHEUS_SLOT_SETTING, "stages_per_channel", "每通道实际级数（空值沿用统一级数）", ORPHEUS_VALUE_STRING, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    return orpheus_builtin_iir_bank_register_slots(s, reg);
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
    orpheus_builtin_iir_bank_State* s=(orpheus_builtin_iir_bank_State*)state;
    orpheus_builtin_iir_bank_IO io={0};
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
    return orpheus_builtin_iir_bank_algorithm(s, &io, ctx);
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
