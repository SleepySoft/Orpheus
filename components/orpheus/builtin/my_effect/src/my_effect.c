#include "orpheus_my_effect.h"
#include "my_effect_user.h"

#include <stdlib.h>
#include <string.h>

static const OrpheusParameter my_effect_params[] = {
    { .id = "channels", .name = "通道数", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32,
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .persistent = true, .readback = true, .affects_signature = true },
    { .id = "mix", .name = "混合比", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.5f },
      .min_f32 = 0.0f, .max_f32 = 1.0f,
      .update_policy = ORPHEUS_UPDATE_SMOOTHED,
      .persistent = true }
};

static const OrpheusPort my_effect_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor my_effect_descriptor = {
    .id = "orpheus.builtin.my_effect", .version = "1.0.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = my_effect_ports, .port_count = 2,
    .params = my_effect_params, .param_count = 2,
    .state_size = sizeof(MyEffectState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = true
};

static const OrpheusComponentDescriptor* my_effect_get_descriptor(void) {
    return &my_effect_descriptor;
}

static int my_effect_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(MyEffectState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}

static int my_effect_destroy(void* state) {
    (void)state;  /* v2：状态块由 Runtime 统一管理 */
    return ORPHEUS_OK;
}

static int my_effect_prepare(void* state, const OrpheusConfig* config) {
    MyEffectState* s = (MyEffectState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    s->mix = 0.5f;
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (!config->param_ids[i]) continue;
        if (strcmp(config->param_ids[i], "mix") == 0 &&
            config->param_values[i].type == ORPHEUS_VALUE_FLOAT) {
            s->mix = config->param_values[i].value.f32;
        }
    }
    return my_effect_user_prepare(s, config);
}

static int my_effect_reset(void* state) {
    return my_effect_user_reset((MyEffectState*)state);
}

static int my_effect_process(void* state, const OrpheusProcessContext* ctx) {
    return my_effect_user_process((MyEffectState*)state, ctx);
}

static int my_effect_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    (void)state; (void)param_id; (void)value;
    return ORPHEUS_ERR_UNSUPPORTED;
}

static int my_effect_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    MyEffectState* s = (MyEffectState*)state;
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)s->channels;
        return ORPHEUS_OK;
    }
    if (strcmp(param_id, "mix") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT;
        value->value.f32 = s->mix;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int my_effect_register_slots(void* state, const OrpheusRegistry* reg) {
    MyEffectState* s = (MyEffectState*)state;
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32 = 1, .max_i32 = 32,
                     .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags = ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                              ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, mix, ORPHEUS_SLOT_SETTING, "mix", "混合比",
                     ORPHEUS_VALUE_FLOAT, .min_f32 = 0.0f, .max_f32 = 1.0f,
                     .update_policy = ORPHEUS_UPDATE_SMOOTHED,
                     .flags = ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_DIRECT_WRITE);
    return ORPHEUS_OK;
}

/* 统一 hook：外部注册优先；组件 hook 把消息转给 user 实现（CUSTOM 入口）。 */
static int my_effect_hook(void* state, uint32_t id, uint32_t event,
                       const OrpheusBlob* req, OrpheusBlob* resp) {
    return my_effect_user_handle((MyEffectState*)state, id, event, req, resp);
}

static const OrpheusComponentInterface my_effect_interface = {
    .get_descriptor = my_effect_get_descriptor, .create = my_effect_create, .destroy = my_effect_destroy,
    .prepare = my_effect_prepare, .reset = my_effect_reset, .process = my_effect_process,
    .set_parameter = my_effect_set_parameter, .get_parameter = my_effect_get_parameter,
    .get_state_value = NULL, .register_slots = my_effect_register_slots,
    .hook = my_effect_hook
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {
    return &my_effect_interface;
}
