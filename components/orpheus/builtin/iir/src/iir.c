#include "orpheus_iir.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const OrpheusParameter params[] = {
    {.id = "channels", .name = "通道数", .type = ORPHEUS_VALUE_INT,
     .default_value = {.type = ORPHEUS_VALUE_INT, .value.i32 = 2},
     .min_i32 = 1, .max_i32 = IIR_MAX_CHANNELS,
     .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
     .readback = true, .persistent = true, .affects_signature = true},
    {.id = "order", .name = "多项式阶数", .type = ORPHEUS_VALUE_INT,
     .default_value = {.type = ORPHEUS_VALUE_INT, .value.i32 = 1},
     .min_i32 = 1, .max_i32 = IIR_MAX_ORDER,
     .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED, .readback = true, .persistent = true},
    {.id = "coefs_mode", .name = "系数模式", .type = ORPHEUS_VALUE_STRING,
     .default_value = {.type = ORPHEUS_VALUE_STRING, .value.str = "shared"},
     .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
     .readback = true, .persistent = true, .affects_signature = true},
    {.id = "coefs", .name = "IIR系数", .type = ORPHEUS_VALUE_STRING,
     .default_value = {.type = ORPHEUS_VALUE_STRING, .value.str = "1,0,0"},
     .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED},
    {.id = "processing_enabled", .name = "处理使能", .type = ORPHEUS_VALUE_INT,
     .default_value = {.type = ORPHEUS_VALUE_INT, .value.i32 = 1},
     .min_i32 = 0, .max_i32 = 1, .update_policy = ORPHEUS_UPDATE_IMMEDIATE,
     .readback = true, .persistent = true}
};

static const OrpheusPort ports[] = {
    {.id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
     .sample_format = ORPHEUS_FORMAT_F32, .is_variable = true, .channels_param = "channels"},
    {.id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
     .sample_format = ORPHEUS_FORMAT_F32, .is_variable = true, .channels_param = "channels"}
};

static const OrpheusComponentDescriptor descriptor = {
    .id = "orpheus.builtin.iir", .version = "1.0.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = ports, .port_count = 2, .params = params, .param_count = 5,
    .state_size = sizeof(IirState), .alignment = 8,
    .realtime_safe = true, .supports_inplace = false
};

static const OrpheusComponentDescriptor* get_descriptor(void) { return &descriptor; }

static int create(void** state, const OrpheusConfig* config) {
    if (config && config->state_block) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(IirState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}

static int destroy(void* state) { (void)state; return ORPHEUS_OK; }

static int prepare(void* state, const OrpheusConfig* config) {
    if (!state || !config) return ORPHEUS_ERR_INVALID_ARG;
    IirState* s = (IirState*)state;
    s->channels = config->channels ? config->channels : 2;
    if (s->channels > IIR_MAX_CHANNELS) return ORPHEUS_ERR_INVALID_ARG;
    s->order = 1;
    s->coefs_mode = 0;
    s->processing_enabled = 1;
    const char* text = "1,0,0";
    for (uint32_t i = 0; i < config->param_count; ++i) {
        const char* id = config->param_ids[i];
        const OrpheusValue* value = &config->param_values[i];
        if (!id) continue;
        if (strcmp(id, "order") == 0) {
            if (value->type != ORPHEUS_VALUE_INT || value->value.i32 < 1 ||
                value->value.i32 > IIR_MAX_ORDER) return ORPHEUS_ERR_INVALID_ARG;
            s->order = (uint32_t)value->value.i32;
        } else if (strcmp(id, "coefs_mode") == 0) {
            if (value->type != ORPHEUS_VALUE_STRING || !value->value.str)
                return ORPHEUS_ERR_INVALID_ARG;
            if (strcmp(value->value.str, "per_channel") == 0) s->coefs_mode = 1;
            else if (strcmp(value->value.str, "shared") == 0) s->coefs_mode = 0;
            else return ORPHEUS_ERR_INVALID_ARG;
        } else if (strcmp(id, "coefs") == 0) {
            if (value->type != ORPHEUS_VALUE_STRING || !value->value.str)
                return ORPHEUS_ERR_INVALID_ARG;
            text = value->value.str;
        } else if (strcmp(id, "processing_enabled") == 0) {
            if (value->type != ORPHEUS_VALUE_INT || value->value.i32 < 0 ||
                value->value.i32 > 1) return ORPHEUS_ERR_INVALID_ARG;
            s->processing_enabled = value->value.i32;
        }
    }
    memset(s->coefs, 0, sizeof(s->coefs));
    const uint32_t width = 2*s->order+1;
    const uint32_t count = (s->coefs_mode ? s->channels : 1)*width;
    const char* p = text;
    for (uint32_t i = 0; i < count; ++i) {
        char* end = NULL;
        float value = strtof(p, &end);
        if (end == p || !isfinite(value)) return ORPHEUS_ERR_INVALID_ARG;
        s->coefs[i/width][i%width] = value;
        p = end;
        while (isspace((unsigned char)*p)) ++p;
        if (i+1 == count) {
            if (*p) return ORPHEUS_ERR_INVALID_ARG;
        } else {
            if (*p != ',') return ORPHEUS_ERR_INVALID_ARG;
            ++p;
        }
    }
    memset(s->history, 0, sizeof(s->history));
    return ORPHEUS_OK;
}

static int reset(void* state) {
    if (!state) return ORPHEUS_ERR_INVALID_ARG;
    memset(((IirState*)state)->history, 0, sizeof(((IirState*)state)->history));
    return ORPHEUS_OK;
}

static int process(void* state, const OrpheusProcessContext* ctx) {
    IirState* s = (IirState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (!out || !out->data) return ORPHEUS_ERR_INVALID_ARG;
    if (!s->processing_enabled) {
        memset(out->data, 0, (size_t)ctx->frame_count*s->channels*sizeof(float));
        out->frame_count = ctx->frame_count;
        return ORPHEUS_OK;
    }
    if (!in || !in->data) return ORPHEUS_ERR_INVALID_ARG;
    const float* input = (const float*)in->data;
    float* output = (float*)out->data;
    for (uint32_t n = 0; n < ctx->frame_count; ++n) {
        for (uint32_t ch = 0; ch < s->channels; ++ch) {
            const float* c = s->coefs[s->coefs_mode ? ch : 0];
            float* z = s->history[ch];
            const float x = input[n*s->channels+ch];
            const float y = c[0]*x+z[0];
            for (uint32_t k = 0; k+1 < s->order; ++k)
                z[k] = (z[k+1]+c[k+1]*x)-c[s->order+k+1]*y;
            z[s->order-1] = c[s->order]*x-c[2*s->order]*y;
            output[n*s->channels+ch] = y;
        }
    }
    out->frame_count = ctx->frame_count;
    return ORPHEUS_OK;
}

static int set_parameter(void* state, const char* id, const OrpheusValue* value) {
    if (!state || !id || !value) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(id, "processing_enabled") != 0) return ORPHEUS_ERR_UNSUPPORTED;
    if (value->type != ORPHEUS_VALUE_INT || value->value.i32 < 0 || value->value.i32 > 1)
        return ORPHEUS_ERR_INVALID_ARG;
    ((IirState*)state)->processing_enabled = value->value.i32;
    return ORPHEUS_OK;
}

static int get_parameter(void* state, const char* id, OrpheusValue* value) {
    if (!state || !id || !value) return ORPHEUS_ERR_INVALID_ARG;
    IirState* s = (IirState*)state;
    value->type = ORPHEUS_VALUE_INT;
    if (strcmp(id, "processing_enabled") == 0) value->value.i32 = s->processing_enabled;
    else if (strcmp(id, "channels") == 0) value->value.i32 = (int32_t)s->channels;
    else if (strcmp(id, "order") == 0) value->value.i32 = (int32_t)s->order;
    else if (strcmp(id, "coefs_mode") == 0) {
        value->type = ORPHEUS_VALUE_STRING;
        value->value.str = s->coefs_mode ? "per_channel" : "shared";
    } else return ORPHEUS_ERR_NOT_FOUND;
    return ORPHEUS_OK;
}

static int register_slots(void* state, const OrpheusRegistry* reg) {
    IirState* s = (IirState*)state;
    OrpheusSlotInfo slot = {
        .kind = ORPHEUS_SLOT_BULK, .key = s->coefs_mode ? "coefs_per_channel" : "coefs",
        .name = "直接型IIR系数", .type = ORPHEUS_VALUE_FLOAT,
        .offset = offsetof(IirState, coefs), .size = sizeof(float),
        .count = (s->coefs_mode ? s->channels : 1)*IIR_COEF_STRIDE,
        .flags = ORPHEUS_SLOT_DOUBLE_BUFFERED
    };
    reg->add(reg->ctx, &slot);
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                    ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=IIR_MAX_CHANNELS,
                    .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                    .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, order, ORPHEUS_SLOT_SETTING, "order", "阶数",
                    ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=IIR_MAX_ORDER,
                    .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                    .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, coefs_mode, ORPHEUS_SLOT_SETTING, "coefs_mode", "系数模式",
                    ORPHEUS_VALUE_INT, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                    .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, processing_enabled, ORPHEUS_SLOT_SETTING, "processing_enabled", "处理使能",
                    ORPHEUS_VALUE_INT, .min_i32=0, .max_i32=1, .update_policy=ORPHEUS_UPDATE_IMMEDIATE,
                    .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface interface = {
    .get_descriptor = get_descriptor, .create = create, .destroy = destroy,
    .prepare = prepare, .reset = reset, .process = process,
    .set_parameter = set_parameter, .get_parameter = get_parameter, .register_slots = register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &interface; }
