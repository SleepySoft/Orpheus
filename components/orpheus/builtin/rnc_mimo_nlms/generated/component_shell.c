#include "component_state.h"
#include <string.h>
#include <math.h>

_Static_assert(_Generic(&(((orpheus_builtin_rnc_mimo_nlms_State*)0)->algorithm.step_sizes), float (*)[8]: 1, default: 0),
    "BULK step_sizes: storage 类型或固定容量不匹配");
_Static_assert(_Generic(&(((orpheus_builtin_rnc_mimo_nlms_State*)0)->algorithm.weights), float (*)[12000]: 1, default: 0),
    "BULK initial_weights: storage 类型或固定容量不匹配");
static const OrpheusPort ports[] = {
    {.id="ref", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="reference_channels"},
    {.id="filtered_error", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="output_channels"},
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="output_channels"},
};

static const OrpheusParameter params[] = {
    {.id="reference_channels", .name="参考通道数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=12}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=1, .max_i32=12},
    {.id="output_channels", .name="输出通道数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=8}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=1, .max_i32=8},
    {.id="filter_length", .name="自适应滤波长度", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=125}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=1, .max_i32=125},
    {.id="step_sizes", .name="各输出步长", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str=""}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=false, .persistent=true, .affects_signature=false},
    {.id="leakage", .name="泄漏因子", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=1.000000000e+00f}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_f32=0.000000000e+00f, .max_f32=1.000000000e+00f},
    {.id="eps", .name="归一化正则项", .type=ORPHEUS_VALUE_FLOAT, .default_value={.type=ORPHEUS_VALUE_FLOAT, .value.f32=1.000000000e-05f}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_f32=1.000000000e-12f, .max_f32=1.000000000e+00f},
    {.id="initial_weights", .name="初始权值", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str=""}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=false, .persistent=true, .affects_signature=false},
};

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.rnc_mimo_nlms", .version="1.1.0", .abi_version=7,
    .ports=ports, .port_count=3,
    .params=params, .param_count=7,
    .state_size=sizeof(orpheus_builtin_rnc_mimo_nlms_State), .alignment=_Alignof(orpheus_builtin_rnc_mimo_nlms_State),
    .scratch_size=0, .latency_samples=0,
    .realtime_safe=true, .supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

static void snapshot_parameters(const orpheus_builtin_rnc_mimo_nlms_State* s, orpheus_builtin_rnc_mimo_nlms_Parameters* p) {
    (void)s; memset(p, 0, sizeof(*p));
    p->reference_channels=s->reference_channels;
    p->output_channels=s->output_channels;
    p->filter_length=s->filter_length;
    memcpy(p->step_sizes, s->step_sizes, sizeof(p->step_sizes));
    p->leakage=s->leakage;
    p->eps=s->eps;
    memcpy(p->initial_weights, s->initial_weights, sizeof(p->initial_weights));
}
static void commit_parameters(orpheus_builtin_rnc_mimo_nlms_State* s, const orpheus_builtin_rnc_mimo_nlms_Parameters* p) {
    (void)s; (void)p;
    s->reference_channels=p->reference_channels;
    s->output_channels=p->output_channels;
    s->filter_length=p->filter_length;
    memcpy(s->step_sizes, p->step_sizes, sizeof(s->step_sizes));
    s->leakage=p->leakage;
    s->eps=p->eps;
    memcpy(s->initial_weights, p->initial_weights, sizeof(s->initial_weights));
}
static void default_parameters(orpheus_builtin_rnc_mimo_nlms_Parameters* p) {
    memset(p, 0, sizeof(*p));
    p->reference_channels=12;
    p->output_channels=8;
    p->filter_length=125;
    memcpy(p->step_sizes, "", 1);
    p->leakage=1.000000000e+00f;
    p->eps=1.000000000e-05f;
    memcpy(p->initial_weights, "", 1);
}
static int resource_count_step_sizes(const orpheus_builtin_rnc_mimo_nlms_Parameters* p, uint32_t* count) {
    (void)p; uint32_t product=1;
    int64_t dimension0=p->output_channels;
    if (dimension0<1 || (uint64_t)dimension0>8u/product) return ORPHEUS_ERR_INVALID_ARG;
    product*=(uint32_t)dimension0;
    *count=product; return ORPHEUS_OK;
}
static int resource_count_initial_weights(const orpheus_builtin_rnc_mimo_nlms_Parameters* p, uint32_t* count) {
    (void)p; uint32_t product=1;
    int64_t dimension0=p->output_channels;
    if (dimension0<1 || (uint64_t)dimension0>12000u/product) return ORPHEUS_ERR_INVALID_ARG;
    product*=(uint32_t)dimension0;
    int64_t dimension1=p->reference_channels;
    if (dimension1<1 || (uint64_t)dimension1>12000u/product) return ORPHEUS_ERR_INVALID_ARG;
    product*=(uint32_t)dimension1;
    int64_t dimension2=p->filter_length;
    if (dimension2<1 || (uint64_t)dimension2>12000u/product) return ORPHEUS_ERR_INVALID_ARG;
    product*=(uint32_t)dimension2;
    *count=product; return ORPHEUS_OK;
}
static int validate_resource_counts(const orpheus_builtin_rnc_mimo_nlms_Parameters* p) {
    uint32_t count; int result;
    result=resource_count_step_sizes(p, &count);
    if (result!=ORPHEUS_OK) return result;
    result=resource_count_initial_weights(p, &count);
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
static int update_parameter(orpheus_builtin_rnc_mimo_nlms_Parameters* p, const char* key, const OrpheusValue* value) {
    if (!p || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "reference_channels") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>12) return ORPHEUS_ERR_INVALID_ARG;
        p->reference_channels=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "output_channels") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>8) return ORPHEUS_ERR_INVALID_ARG;
        p->output_channels=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "filter_length") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>125) return ORPHEUS_ERR_INVALID_ARG;
        p->filter_length=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "step_sizes") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->step_sizes), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->step_sizes)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->step_sizes, text, sizeof(text));
        return ORPHEUS_OK;
    }
    if (strcmp(key, "leakage") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<0.000000000e+00f || value->value.f32>1.000000000e+00f) return ORPHEUS_ERR_INVALID_ARG;
        p->leakage=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "eps") == 0) {
        if (value->type!=ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
        if (!isfinite(value->value.f32)) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.f32<1.000000000e-12f || value->value.f32>1.000000000e+00f) return ORPHEUS_ERR_INVALID_ARG;
        p->eps=value->value.f32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "initial_weights") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->initial_weights), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->initial_weights)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->initial_weights, text, sizeof(text));
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int set_parameter(void* state, const char* key, const OrpheusValue* value) {
    orpheus_builtin_rnc_mimo_nlms_State* s=(orpheus_builtin_rnc_mimo_nlms_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_rnc_mimo_nlms_Parameters before, candidate; snapshot_parameters(s, &before); candidate=before;
    int result=update_parameter(&candidate, key, value);
    if (result!=ORPHEUS_OK) return result;
    result=validate_resource_counts(&candidate); if (result!=ORPHEUS_OK) return result;
    result=orpheus_builtin_rnc_mimo_nlms_validate(&candidate); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate); return ORPHEUS_OK;
}
static int get_parameter(void* state, const char* key, OrpheusValue* value) {
    orpheus_builtin_rnc_mimo_nlms_State* s=(orpheus_builtin_rnc_mimo_nlms_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "reference_channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->reference_channels; return ORPHEUS_OK;
    }
    if (strcmp(key, "output_channels")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->output_channels; return ORPHEUS_OK;
    }
    if (strcmp(key, "filter_length")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->filter_length; return ORPHEUS_OK;
    }
    if (strcmp(key, "step_sizes")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->step_sizes; return ORPHEUS_OK;
    }
    if (strcmp(key, "leakage")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->leakage; return ORPHEUS_OK;
    }
    if (strcmp(key, "eps")==0) {
        value->type=ORPHEUS_VALUE_FLOAT; value->value.f32=s->eps; return ORPHEUS_OK;
    }
    if (strcmp(key, "initial_weights")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->initial_weights; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int configure_parameters(orpheus_builtin_rnc_mimo_nlms_Parameters* p, const OrpheusConfig* config) {
    if (!config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    default_parameters(p);
    for (uint32_t i=0; i<config->param_count; ++i) {
        int result=update_parameter(p, config->param_ids[i], &config->param_values[i]);
        if (result!=ORPHEUS_OK) return result;
    }
    int result=validate_resource_counts(p); if (result!=ORPHEUS_OK) return result;
    return orpheus_builtin_rnc_mimo_nlms_validate(p);
}
static int cleanup(orpheus_builtin_rnc_mimo_nlms_State* s) {
    int result=ORPHEUS_OK;
    if (s->_orpheus_cleanup_pending) {
        s->_orpheus_cleanup_pending=false;
        memset(&s->algorithm, 0, sizeof(s->algorithm));
    }
    return result;
}
static int prepare(void* state, const OrpheusConfig* config) {
    orpheus_builtin_rnc_mimo_nlms_State* s=(orpheus_builtin_rnc_mimo_nlms_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_rnc_mimo_nlms_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    result=cleanup(s); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate);
    s->_orpheus_cleanup_pending=true;
    result=orpheus_builtin_rnc_mimo_nlms_prepare(s, config);
    if (result!=ORPHEUS_OK) { (void)cleanup(s); return result; }
    return ORPHEUS_OK;
}
static int create(void** state, const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=NULL;
    if (!config || !config->state_block) return ORPHEUS_ERR_INVALID_ARG;
    if ((uintptr_t)config->state_block % descriptor.alignment) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_rnc_mimo_nlms_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    orpheus_builtin_rnc_mimo_nlms_State* s=(orpheus_builtin_rnc_mimo_nlms_State*)config->state_block;
    memset(s, 0, sizeof(*s)); commit_parameters(s, &candidate);
    s->_orpheus_alive=true; *state=s; return ORPHEUS_OK;
}
static int destroy(void* state) {
    orpheus_builtin_rnc_mimo_nlms_State* s=(orpheus_builtin_rnc_mimo_nlms_State*)state;
    if (!s) return ORPHEUS_ERR_INVALID_ARG;
    if (!s->_orpheus_alive) return ORPHEUS_OK;
    int result=cleanup(s); s->_orpheus_alive=false; return result;
}
static int reset(void* state) {
    orpheus_builtin_rnc_mimo_nlms_State* s=(orpheus_builtin_rnc_mimo_nlms_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_builtin_rnc_mimo_nlms_reset(s);
}
int orpheus_builtin_rnc_mimo_nlms_register_step_sizes(orpheus_builtin_rnc_mimo_nlms_State* s, const OrpheusRegistry* reg) {
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_rnc_mimo_nlms_Parameters current; snapshot_parameters(s, &current);
    uint32_t count_step_sizes;
    if (resource_count_step_sizes(&current, &count_step_sizes)!=ORPHEUS_OK) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_ARRAY(reg, s, algorithm.step_sizes, count_step_sizes, ORPHEUS_SLOT_BULK, "step_sizes", "各输出步长", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    return ORPHEUS_OK;
}
int orpheus_builtin_rnc_mimo_nlms_register_initial_weights(orpheus_builtin_rnc_mimo_nlms_State* s, const OrpheusRegistry* reg) {
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_rnc_mimo_nlms_Parameters current; snapshot_parameters(s, &current);
    uint32_t count_initial_weights;
    if (resource_count_initial_weights(&current, &count_initial_weights)!=ORPHEUS_OK) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_ARRAY(reg, s, algorithm.weights, count_initial_weights, ORPHEUS_SLOT_BULK, "weights", "初始权值", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_DOUBLE_BUFFERED)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    return ORPHEUS_OK;
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    orpheus_builtin_rnc_mimo_nlms_State* s=(orpheus_builtin_rnc_mimo_nlms_State*)state;
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_SLOT(reg, s, reference_channels, ORPHEUS_SLOT_SETTING, "reference_channels", "参考通道数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=1, .max_i32=12)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, output_channels, ORPHEUS_SLOT_SETTING, "output_channels", "输出通道数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=1, .max_i32=8)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, filter_length, ORPHEUS_SLOT_SETTING, "filter_length", "自适应滤波长度", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=1, .max_i32=125)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    int result_step_sizes=orpheus_builtin_rnc_mimo_nlms_register_step_sizes(s, reg);
    if (result_step_sizes!=ORPHEUS_OK) return result_step_sizes;
    if (ORPHEUS_REG_SLOT(reg, s, leakage, ORPHEUS_SLOT_SETTING, "leakage", "泄漏因子", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=0.000000000e+00f, .max_f32=1.000000000e+00f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, eps, ORPHEUS_SLOT_SETTING, "eps", "归一化正则项", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_f32=1.000000000e-12f, .max_f32=1.000000000e+00f)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    int result_initial_weights=orpheus_builtin_rnc_mimo_nlms_register_initial_weights(s, reg);
    if (result_initial_weights!=ORPHEUS_OK) return result_initial_weights;
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
    orpheus_builtin_rnc_mimo_nlms_State* s=(orpheus_builtin_rnc_mimo_nlms_State*)state;
    orpheus_builtin_rnc_mimo_nlms_IO io={0};
    if (!s || !s->_orpheus_alive || !ctx) return ORPHEUS_ERR_INVALID_ARG;
    io.ref.input_index=0;
    io.ref.buffer=(ctx->inputs && ctx->input_count > (0)) ? ctx->inputs[0] : NULL;
    if (!valid_buffer(io.ref.buffer, (uint32_t)(s->reference_channels),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.ref.samples=io.ref.buffer ? (const float*)io.ref.buffer->data : NULL;
    io.filtered_error.input_index=1;
    io.filtered_error.buffer=(ctx->inputs && ctx->input_count > (1)) ? ctx->inputs[1] : NULL;
    if (!valid_buffer(io.filtered_error.buffer, (uint32_t)(s->output_channels),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.filtered_error.samples=io.filtered_error.buffer ? (const float*)io.filtered_error.buffer->data : NULL;
    io.out.active=true;
    io.out.drive_port_no=1u;
    io.out.buffer=(ctx->outputs && ctx->output_count > (0)) ? ctx->outputs[0] : NULL;
    if (!valid_buffer(io.out.buffer, (uint32_t)(s->output_channels),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.out.samples=io.out.buffer ? (float*)io.out.buffer->data : NULL;
    if (io.out.buffer) {
        if (ctx->frame_count > io.out.buffer->frame_capacity) return ORPHEUS_ERR_INVALID_ARG;
        io.out.buffer->frame_count=ctx->frame_count;
    }
    io.out.delivery=(OrpheusDelivery){.buffer=io.out.buffer, .frame_index=ctx->frame_index,
        .epoch=ctx->epoch, .timeline_flags=ctx->timeline_flags};
    return orpheus_builtin_rnc_mimo_nlms_algorithm(s, &io, ctx);
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
