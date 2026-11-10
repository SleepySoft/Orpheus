#include "component_state.h"
#include <string.h>
#include <math.h>

_Static_assert(_Generic(&(((orpheus_builtin_channel_selector_State*)0)->algorithm.indices), int32_t (*)[64]: 1, default: 0),
    "BULK indices: storage 类型或固定容量不匹配");
_Static_assert(_Generic(&(((orpheus_builtin_channel_selector_State*)0)->algorithm.gains), float (*)[64]: 1, default: 0),
    "BULK gains: storage 类型或固定容量不匹配");
static const OrpheusPort ports[] = {
    {.id="a", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels_a"},
    {.id="b", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels_b"},
    {.id="base", .direction=ORPHEUS_PORT_INPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels_out"},
    {.id="out", .direction=ORPHEUS_PORT_OUTPUT, .type=ORPHEUS_PORT_AUDIO, .sample_format=ORPHEUS_FORMAT_F32, .channels=0, .sample_rate=0, .block_size=0, .is_variable=true, .channels_param="channels_out"},
};

static const OrpheusParameter params[] = {
    {.id="channels_a", .name="A输入通道数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=2}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=1, .max_i32=64},
    {.id="channels_b", .name="B输入通道数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=2}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=1, .max_i32=64},
    {.id="channels_out", .name="输出通道数", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=2}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=true, .min_i32=1, .max_i32=64},
    {.id="index_base", .name="索引起点（0或1）", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=0}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_i32=0, .max_i32=1},
    {.id="high_index_policy", .name="正越界策略（0置零，1夹到末路）", .type=ORPHEUS_VALUE_INT, .default_value={.type=ORPHEUS_VALUE_INT, .value.i32=0}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false, .min_i32=0, .max_i32=1},
    {.id="apply_gain", .name="应用逐通道增益", .type=ORPHEUS_VALUE_BOOL, .default_value={.type=ORPHEUS_VALUE_BOOL, .value.b=false}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="indices", .name="初始通道索引", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str=""}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="gains", .name="初始逐通道线性增益", .type=ORPHEUS_VALUE_STRING, .default_value={.type=ORPHEUS_VALUE_STRING, .value.str=""}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="accumulate", .name="叠加到基底输入", .type=ORPHEUS_VALUE_BOOL, .default_value={.type=ORPHEUS_VALUE_BOOL, .value.b=false}, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .readback=true, .persistent=true, .affects_signature=false},
    {.id="enabled", .name="选择分支使能", .type=ORPHEUS_VALUE_BOOL, .default_value={.type=ORPHEUS_VALUE_BOOL, .value.b=true}, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .readback=true, .persistent=true, .affects_signature=false},
};

static const OrpheusComponentDescriptor descriptor = {
    .id="orpheus.builtin.channel_selector", .version="1.2.0", .abi_version=7,
    .ports=ports, .port_count=4,
    .params=params, .param_count=10,
    .state_size=sizeof(orpheus_builtin_channel_selector_State), .alignment=_Alignof(orpheus_builtin_channel_selector_State),
    .scratch_size=0, .latency_samples=0,
    .realtime_safe=true, .supports_inplace=false
};
static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

static void snapshot_parameters(const orpheus_builtin_channel_selector_State* s, orpheus_builtin_channel_selector_Parameters* p) {
    (void)s; memset(p, 0, sizeof(*p));
    p->channels_a=s->channels_a;
    p->channels_b=s->channels_b;
    p->channels_out=s->channels_out;
    p->index_base=s->index_base;
    p->high_index_policy=s->high_index_policy;
    p->apply_gain=s->apply_gain;
    memcpy(p->indices, s->indices, sizeof(p->indices));
    memcpy(p->gains, s->gains, sizeof(p->gains));
    p->accumulate=s->accumulate;
    p->enabled=s->enabled;
}
static void commit_parameters(orpheus_builtin_channel_selector_State* s, const orpheus_builtin_channel_selector_Parameters* p) {
    (void)s; (void)p;
    s->channels_a=p->channels_a;
    s->channels_b=p->channels_b;
    s->channels_out=p->channels_out;
    s->index_base=p->index_base;
    s->high_index_policy=p->high_index_policy;
    s->apply_gain=p->apply_gain;
    memcpy(s->indices, p->indices, sizeof(s->indices));
    memcpy(s->gains, p->gains, sizeof(s->gains));
    s->accumulate=p->accumulate;
    s->enabled=p->enabled;
}
static void default_parameters(orpheus_builtin_channel_selector_Parameters* p) {
    memset(p, 0, sizeof(*p));
    p->channels_a=2;
    p->channels_b=2;
    p->channels_out=2;
    p->index_base=0;
    p->high_index_policy=0;
    p->apply_gain=false;
    memcpy(p->indices, "", 1);
    memcpy(p->gains, "", 1);
    p->accumulate=false;
    p->enabled=true;
}
static int resource_count_indices(const orpheus_builtin_channel_selector_Parameters* p, uint32_t* count) {
    (void)p; uint32_t product=1;
    int64_t dimension0=p->channels_out;
    if (dimension0<1 || (uint64_t)dimension0>64u/product) return ORPHEUS_ERR_INVALID_ARG;
    product*=(uint32_t)dimension0;
    *count=product; return ORPHEUS_OK;
}
static int resource_count_gains(const orpheus_builtin_channel_selector_Parameters* p, uint32_t* count) {
    (void)p; uint32_t product=1;
    int64_t dimension0=p->channels_out;
    if (dimension0<1 || (uint64_t)dimension0>64u/product) return ORPHEUS_ERR_INVALID_ARG;
    product*=(uint32_t)dimension0;
    *count=product; return ORPHEUS_OK;
}
static int validate_resource_counts(const orpheus_builtin_channel_selector_Parameters* p) {
    uint32_t count; int result;
    result=resource_count_indices(p, &count);
    if (result!=ORPHEUS_OK) return result;
    result=resource_count_gains(p, &count);
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
static int update_parameter(orpheus_builtin_channel_selector_Parameters* p, const char* key, const OrpheusValue* value) {
    if (!p || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "channels_a") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>64) return ORPHEUS_ERR_INVALID_ARG;
        p->channels_a=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "channels_b") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>64) return ORPHEUS_ERR_INVALID_ARG;
        p->channels_b=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "channels_out") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<1 || value->value.i32>64) return ORPHEUS_ERR_INVALID_ARG;
        p->channels_out=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "index_base") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<0 || value->value.i32>1) return ORPHEUS_ERR_INVALID_ARG;
        p->index_base=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "high_index_policy") == 0) {
        if (value->type!=ORPHEUS_VALUE_INT) return ORPHEUS_ERR_INVALID_ARG;
        if (value->value.i32<0 || value->value.i32>1) return ORPHEUS_ERR_INVALID_ARG;
        p->high_index_policy=value->value.i32;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "apply_gain") == 0) {
        if (value->type!=ORPHEUS_VALUE_BOOL) return ORPHEUS_ERR_INVALID_ARG;
        p->apply_gain=value->value.b;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "indices") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->indices), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->indices)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->indices, text, sizeof(text));
        return ORPHEUS_OK;
    }
    if (strcmp(key, "gains") == 0) {
        if (value->type!=ORPHEUS_VALUE_STRING) return ORPHEUS_ERR_INVALID_ARG;
        size_t length;
        if (valid_utf8_string(value->value.str, sizeof(p->gains), &length)!=ORPHEUS_OK)
            return ORPHEUS_ERR_INVALID_ARG;
        char text[sizeof(p->gains)]; memset(text, 0, sizeof(text));
        memcpy(text, value->value.str, length+1);
        memcpy(p->gains, text, sizeof(text));
        return ORPHEUS_OK;
    }
    if (strcmp(key, "accumulate") == 0) {
        if (value->type!=ORPHEUS_VALUE_BOOL) return ORPHEUS_ERR_INVALID_ARG;
        p->accumulate=value->value.b;
        return ORPHEUS_OK;
    }
    if (strcmp(key, "enabled") == 0) {
        if (value->type!=ORPHEUS_VALUE_BOOL) return ORPHEUS_ERR_INVALID_ARG;
        p->enabled=value->value.b;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int set_parameter(void* state, const char* key, const OrpheusValue* value) {
    orpheus_builtin_channel_selector_State* s=(orpheus_builtin_channel_selector_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_channel_selector_Parameters before, candidate; snapshot_parameters(s, &before); candidate=before;
    int result=update_parameter(&candidate, key, value);
    if (result!=ORPHEUS_OK) return result;
    result=validate_resource_counts(&candidate); if (result!=ORPHEUS_OK) return result;
    result=orpheus_builtin_channel_selector_validate(&candidate); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate); return ORPHEUS_OK;
}
static int get_parameter(void* state, const char* key, OrpheusValue* value) {
    orpheus_builtin_channel_selector_State* s=(orpheus_builtin_channel_selector_State*)state;
    if (!s || !s->_orpheus_alive || !key || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(key, "channels_a")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->channels_a; return ORPHEUS_OK;
    }
    if (strcmp(key, "channels_b")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->channels_b; return ORPHEUS_OK;
    }
    if (strcmp(key, "channels_out")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->channels_out; return ORPHEUS_OK;
    }
    if (strcmp(key, "index_base")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->index_base; return ORPHEUS_OK;
    }
    if (strcmp(key, "high_index_policy")==0) {
        value->type=ORPHEUS_VALUE_INT; value->value.i32=s->high_index_policy; return ORPHEUS_OK;
    }
    if (strcmp(key, "apply_gain")==0) {
        value->type=ORPHEUS_VALUE_BOOL; value->value.b=s->apply_gain; return ORPHEUS_OK;
    }
    if (strcmp(key, "indices")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->indices; return ORPHEUS_OK;
    }
    if (strcmp(key, "gains")==0) {
        value->type=ORPHEUS_VALUE_STRING; value->value.str=s->gains; return ORPHEUS_OK;
    }
    if (strcmp(key, "accumulate")==0) {
        value->type=ORPHEUS_VALUE_BOOL; value->value.b=s->accumulate; return ORPHEUS_OK;
    }
    if (strcmp(key, "enabled")==0) {
        value->type=ORPHEUS_VALUE_BOOL; value->value.b=s->enabled; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int configure_parameters(orpheus_builtin_channel_selector_Parameters* p, const OrpheusConfig* config) {
    if (!config || (config->param_count && (!config->param_ids || !config->param_values)))
        return ORPHEUS_ERR_INVALID_ARG;
    default_parameters(p);
    for (uint32_t i=0; i<config->param_count; ++i) {
        int result=update_parameter(p, config->param_ids[i], &config->param_values[i]);
        if (result!=ORPHEUS_OK) return result;
    }
    int result=validate_resource_counts(p); if (result!=ORPHEUS_OK) return result;
    return orpheus_builtin_channel_selector_validate(p);
}
static int cleanup(orpheus_builtin_channel_selector_State* s) {
    int result=ORPHEUS_OK;
    if (s->_orpheus_cleanup_pending) {
        s->_orpheus_cleanup_pending=false;
        memset(&s->algorithm, 0, sizeof(s->algorithm));
    }
    return result;
}
static int prepare(void* state, const OrpheusConfig* config) {
    orpheus_builtin_channel_selector_State* s=(orpheus_builtin_channel_selector_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_channel_selector_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    result=cleanup(s); if (result!=ORPHEUS_OK) return result;
    commit_parameters(s, &candidate);
    s->_orpheus_cleanup_pending=true;
    result=orpheus_builtin_channel_selector_prepare(s, config);
    if (result!=ORPHEUS_OK) { (void)cleanup(s); return result; }
    return ORPHEUS_OK;
}
static int create(void** state, const OrpheusConfig* config) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    *state=NULL;
    if (!config || !config->state_block) return ORPHEUS_ERR_INVALID_ARG;
    if ((uintptr_t)config->state_block % descriptor.alignment) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_channel_selector_Parameters candidate; int result=configure_parameters(&candidate, config);
    if (result!=ORPHEUS_OK) return result;
    orpheus_builtin_channel_selector_State* s=(orpheus_builtin_channel_selector_State*)config->state_block;
    memset(s, 0, sizeof(*s)); commit_parameters(s, &candidate);
    s->_orpheus_alive=true; *state=s; return ORPHEUS_OK;
}
static int destroy(void* state) {
    orpheus_builtin_channel_selector_State* s=(orpheus_builtin_channel_selector_State*)state;
    if (!s) return ORPHEUS_ERR_INVALID_ARG;
    if (!s->_orpheus_alive) return ORPHEUS_OK;
    int result=cleanup(s); s->_orpheus_alive=false; return result;
}
static int reset(void* state) {
    orpheus_builtin_channel_selector_State* s=(orpheus_builtin_channel_selector_State*)state;
    if (!s || !s->_orpheus_alive) return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_builtin_channel_selector_reset(s);
}
int orpheus_builtin_channel_selector_register_indices(orpheus_builtin_channel_selector_State* s, const OrpheusRegistry* reg) {
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_channel_selector_Parameters current; snapshot_parameters(s, &current);
    uint32_t count_indices;
    if (resource_count_indices(&current, &count_indices)!=ORPHEUS_OK) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_ARRAY(reg, s, algorithm.indices, count_indices, ORPHEUS_SLOT_BULK, "indices", "初始通道索引", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_DOUBLE_BUFFERED)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    return ORPHEUS_OK;
}
int orpheus_builtin_channel_selector_register_gains(orpheus_builtin_channel_selector_State* s, const OrpheusRegistry* reg) {
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_channel_selector_Parameters current; snapshot_parameters(s, &current);
    uint32_t count_gains;
    if (resource_count_gains(&current, &count_gains)!=ORPHEUS_OK) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_ARRAY(reg, s, algorithm.gains, count_gains, ORPHEUS_SLOT_BULK, "gains", "初始逐通道线性增益", ORPHEUS_VALUE_FLOAT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_DOUBLE_BUFFERED)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    return ORPHEUS_OK;
}
static int register_slots(void* state, const OrpheusRegistry* reg) {
    orpheus_builtin_channel_selector_State* s=(orpheus_builtin_channel_selector_State*)state;
    if (!s || !s->_orpheus_alive || !reg || !reg->add) return ORPHEUS_ERR_INVALID_ARG;
    if (ORPHEUS_REG_SLOT(reg, s, channels_a, ORPHEUS_SLOT_SETTING, "channels_a", "A输入通道数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=1, .max_i32=64)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, channels_b, ORPHEUS_SLOT_SETTING, "channels_b", "B输入通道数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=1, .max_i32=64)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, channels_out, ORPHEUS_SLOT_SETTING, "channels_out", "输出通道数", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_AFFECTS_SIGNATURE, .min_i32=1, .max_i32=64)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, index_base, ORPHEUS_SLOT_SETTING, "index_base", "索引起点（0或1）", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_i32=0, .max_i32=1)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, high_index_policy, ORPHEUS_SLOT_SETTING, "high_index_policy", "正越界策略（0置零，1夹到末路）", ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT, .min_i32=0, .max_i32=1)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, apply_gain, ORPHEUS_SLOT_SETTING, "apply_gain", "应用逐通道增益", ORPHEUS_VALUE_BOOL, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    int result_indices=orpheus_builtin_channel_selector_register_indices(s, reg);
    if (result_indices!=ORPHEUS_OK) return result_indices;
    int result_gains=orpheus_builtin_channel_selector_register_gains(s, reg);
    if (result_gains!=ORPHEUS_OK) return result_gains;
    if (ORPHEUS_REG_SLOT(reg, s, accumulate, ORPHEUS_SLOT_SETTING, "accumulate", "叠加到基底输入", ORPHEUS_VALUE_BOOL, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
        return ORPHEUS_ERR_PROCESSING;
    if (ORPHEUS_REG_SLOT(reg, s, enabled, ORPHEUS_SLOT_SETTING, "enabled", "选择分支使能", ORPHEUS_VALUE_BOOL, .update_policy=ORPHEUS_UPDATE_BLOCK_BOUNDARY, .unit="", .flags=ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_PERSISTENT)==ORPHEUS_SLOT_ID_INVALID)
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
    orpheus_builtin_channel_selector_State* s=(orpheus_builtin_channel_selector_State*)state;
    orpheus_builtin_channel_selector_IO io={0};
    if (!s || !s->_orpheus_alive || !ctx) return ORPHEUS_ERR_INVALID_ARG;
    io.a.input_index=0;
    io.a.buffer=(ctx->inputs && ctx->input_count > (0)) ? ctx->inputs[0] : NULL;
    if (!valid_buffer(io.a.buffer, (uint32_t)(s->channels_a),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.a.samples=io.a.buffer ? (const float*)io.a.buffer->data : NULL;
    io.b.input_index=1;
    io.b.buffer=(ctx->inputs && ctx->input_count > (1)) ? ctx->inputs[1] : NULL;
    if (!valid_buffer(io.b.buffer, (uint32_t)(s->channels_b),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.b.samples=io.b.buffer ? (const float*)io.b.buffer->data : NULL;
    io.base.input_index=2;
    io.base.buffer=(ctx->inputs && ctx->input_count > (2)) ? ctx->inputs[2] : NULL;
    if (!valid_buffer(io.base.buffer, (uint32_t)(s->channels_out),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.base.samples=io.base.buffer ? (const float*)io.base.buffer->data : NULL;
    io.out.active=true;
    io.out.drive_port_no=1u;
    io.out.buffer=(ctx->outputs && ctx->output_count > (0)) ? ctx->outputs[0] : NULL;
    if (!valid_buffer(io.out.buffer, (uint32_t)(s->channels_out),
        ORPHEUS_FORMAT_F32, sizeof(float), _Alignof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    io.out.samples=io.out.buffer ? (float*)io.out.buffer->data : NULL;
    if (io.out.buffer) {
        if (ctx->frame_count > io.out.buffer->frame_capacity) return ORPHEUS_ERR_INVALID_ARG;
        io.out.buffer->frame_count=ctx->frame_count;
    }
    io.out.delivery=(OrpheusDelivery){.buffer=io.out.buffer, .frame_index=ctx->frame_index,
        .epoch=ctx->epoch, .timeline_flags=ctx->timeline_flags};
    return orpheus_builtin_channel_selector_algorithm(s, &io, ctx);
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
