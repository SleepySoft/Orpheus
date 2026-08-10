#include "orpheus_channel_router.h"

#include <stdlib.h>
#include <string.h>

static uint32_t read_uint(const OrpheusConfig* config, const char* id, uint32_t fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_FLOAT) return (uint32_t)config->param_values[i].value.f32;
            if (config->param_values[i].type == ORPHEUS_VALUE_INT) return (uint32_t)config->param_values[i].value.i32;
        }
    }
    return fallback;
}

static const char* read_str(const OrpheusConfig* config, const char* id, const char* fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_STRING) return config->param_values[i].value.str;
        }
    }
    return fallback;
}

static const OrpheusParameter cr_params[] = {
    { .id = "channels_in", .name = "Input Channels", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "channels_out", .name = "Output Channels", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "indices", .name = "Indices", .type = ORPHEUS_VALUE_STRING,
      .default_value = { .type = ORPHEUS_VALUE_STRING, .value.str = "0,1" },
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = false }
};

static const OrpheusPort cr_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels_in" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels_out" }
};

static const OrpheusComponentDescriptor cr_descriptor = {
    .id = "orpheus.builtin.channel_router", .version = "1.0.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = cr_ports, .port_count = 2, .params = cr_params, .param_count = 3,
    .state_size = sizeof(ChannelRouterState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = false
};

static const OrpheusComponentDescriptor* cr_get_descriptor(void) { return &cr_descriptor; }

static int cr_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(ChannelRouterState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}
static int cr_destroy(void* state) { (void)state; return ORPHEUS_OK; }

static int cr_prepare(void* state, const OrpheusConfig* config) {
    ChannelRouterState* s = (ChannelRouterState*)state;
    s->channels_in = read_uint(config, "channels_in", 2);
    s->channels_out = read_uint(config, "channels_out", 2);
    if (s->channels_in > CR_MAX_CH) s->channels_in = CR_MAX_CH;
    if (s->channels_out > CR_MAX_CH) s->channels_out = CR_MAX_CH;

    /* 默认 identity */
    for (uint32_t o = 0; o < CR_MAX_CH; ++o) {
        s->map[o] = (o < s->channels_in) ? (int32_t)o : -1;
    }

    const char* str = read_str(config, "indices", NULL);
    if (str != NULL) {
        char buf[2048];
        size_t len = strlen(str);
        if (len >= sizeof(buf)) len = sizeof(buf) - 1;
        memcpy(buf, str, len);
        buf[len] = '\0';
        uint32_t o = 0;
        char* p = buf;
        while (p != NULL && *p != '\0' && o < s->channels_out) {
            char* end = NULL;
            long val = strtol(p, &end, 10);
            if (end == p) { ++p; continue; }
            if (val < 0 || (uint32_t)val >= s->channels_in) s->map[o] = -1;
            else s->map[o] = (int32_t)val;
            ++o;
            p = end;
        }
        const char* ss = str != NULL ? str : "0,1";
        strncpy(s->indices_str, ss, sizeof(s->indices_str) - 1);
        s->indices_str[sizeof(s->indices_str) - 1] = '\0';
    }
    return ORPHEUS_OK;
}
static int cr_reset(void* state) { (void)state; return ORPHEUS_OK; }

static int cr_process(void* state, const OrpheusProcessContext* ctx) {
    ChannelRouterState* s = (ChannelRouterState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (in == NULL || out == NULL) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t frames = ctx->frame_count;
    uint32_t cin = in->channels;
    uint32_t cout = out->channels;
    if (cin > CR_MAX_CH) cin = CR_MAX_CH;
    if (cout > CR_MAX_CH) cout = CR_MAX_CH;
    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;
    for (uint32_t n = 0; n < frames; ++n) {
        for (uint32_t o = 0; o < cout; ++o) {
            int32_t src = (o < CR_MAX_CH) ? s->map[o] : -1;
            if (src >= 0 && (uint32_t)src < cin) {
                out_data[n * cout + o] = in_data[n * cin + (uint32_t)src];
            } else {
                out_data[n * cout + o] = 0.0f;
            }
        }
    }
    out->frame_count = frames;
    return ORPHEUS_OK;
}
static int cr_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    (void)state; (void)param_id; (void)value;
    return ORPHEUS_ERR_UNSUPPORTED;
}
static int cr_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    ChannelRouterState* s = (ChannelRouterState*)state;
    if (strcmp(param_id, "channels_in") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->channels_in; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "channels_out") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->channels_out; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}
static int cr_register_slots(void* state, const OrpheusRegistry* reg) {
    ChannelRouterState* s = (ChannelRouterState*)state;
    ORPHEUS_REG_SLOT(reg, s, channels_in, ORPHEUS_SLOT_SETTING, "channels_in", "输入通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, channels_out, ORPHEUS_SLOT_SETTING, "channels_out", "输出通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, indices_str, ORPHEUS_SLOT_SETTING, "indices", "索引映射",
                     ORPHEUS_VALUE_STRING, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    return ORPHEUS_OK;
}
static const OrpheusComponentInterface cr_interface = {
    .get_descriptor = cr_get_descriptor, .create = cr_create, .destroy = cr_destroy,
    .prepare = cr_prepare, .reset = cr_reset, .process = cr_process,
    .set_parameter = cr_set_parameter, .get_parameter = cr_get_parameter, .get_state_value = NULL,
    .register_slots = cr_register_slots
};
#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &cr_interface; }
