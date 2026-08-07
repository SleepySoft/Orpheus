#include "orpheus_embed_out.h"

#include <stdlib.h>
#include <string.h>

static const OrpheusParameter embed_out_params[] = {
    { .id = "channels", .name = "通道数", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32,
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .persistent = true, .readback = true, .affects_signature = true },
    { .id = "sample_rate", .name = "采样率", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 48000 },
      .min_i32 = 1000, .max_i32 = 192000, .unit = "Hz",
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .persistent = true, .readback = true, .affects_signature = true }
};

static const OrpheusPort embed_out_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor embed_out_descriptor = {
    .id = "orpheus.builtin.embed_out", .version = "1.0.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = embed_out_ports, .port_count = 1, .params = embed_out_params, .param_count = 2,
    .state_size = sizeof(EmbedOutState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = false
};

static const OrpheusComponentDescriptor* embed_out_get_descriptor(void) {
    return &embed_out_descriptor;
}

static int embed_out_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(EmbedOutState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}

static int embed_out_destroy(void* state) {
    (void)state;  /* v2：状态块由 Runtime 统一管理 */
    return ORPHEUS_OK;
}

static int embed_out_prepare(void* state, const OrpheusConfig* config) {
    EmbedOutState* s = (EmbedOutState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    s->sample_rate = config->sample_rate;
    s->dst = NULL;
    s->dst_capacity = 0;
    return ORPHEUS_OK;
}

static int embed_out_reset(void* state) {
    (void)state;
    return ORPHEUS_OK;
}

/* 实时安全：无分配、无锁、无 IO——只把输入拷贝到用户消费区。 */
static int embed_out_process(void* state, const OrpheusProcessContext* ctx) {
    EmbedOutState* s = (EmbedOutState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    if (in == NULL) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels > 0 ? s->channels : 1;
    if (s->dst != NULL && s->dst_capacity >= frames) {
        memcpy(s->dst, in->data, (size_t)frames * ch * sizeof(float));
    }
    return ORPHEUS_OK;
}

static int embed_out_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    (void)state; (void)param_id; (void)value;
    return ORPHEUS_ERR_UNSUPPORTED;  /* 全部 restart_required */
}

static int embed_out_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    EmbedOutState* s = (EmbedOutState*)state;
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)s->channels;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "sample_rate") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)s->sample_rate;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int embed_out_register_slots(void* state, const OrpheusRegistry* reg) {
    EmbedOutState* s = (EmbedOutState*)state;
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32 = 1, .max_i32 = 32,
                     .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags = ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                              ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, sample_rate, ORPHEUS_SLOT_SETTING, "sample_rate", "采样率",
                     ORPHEUS_VALUE_INT, .min_i32 = 1000, .max_i32 = 192000, .unit = "Hz",
                     .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags = ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                              ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface embed_out_interface = {
    .get_descriptor = embed_out_get_descriptor, .create = embed_out_create, .destroy = embed_out_destroy,
    .prepare = embed_out_prepare, .reset = embed_out_reset, .process = embed_out_process,
    .set_parameter = embed_out_set_parameter, .get_parameter = embed_out_get_parameter,
    .get_state_value = NULL, .register_slots = embed_out_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {
    return &embed_out_interface;
}
