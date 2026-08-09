#include "orpheus_iir_bank.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- 辅助函数 ---- */

static int32_t read_int(const OrpheusConfig* config, const char* id, int32_t fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_INT)   return config->param_values[i].value.i32;
            if (config->param_values[i].type == ORPHEUS_VALUE_FLOAT) return (int32_t)config->param_values[i].value.f32;
        }
    }
    return fallback;
}

/* ---- 描述符 ---- */

static const OrpheusParameter ib_params[] = {
    { .id = "channels", .name = "Channels", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32,
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "num_stages", .name = "Num Stages", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 4 },
      .min_i32 = 1, .max_i32 = 16,
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true },
    { .id = "coefs", .name = "IIR Coefficients", .type = ORPHEUS_VALUE_STRING,
      .default_value = { .type = ORPHEUS_VALUE_STRING, .value.str = "1,0,0,0,0" },
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = false, .persistent = false }
};

static const OrpheusPort ib_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor ib_descriptor = {
    .id = "orpheus.builtin.iir_bank", .version = "1.0.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = ib_ports, .port_count = 2, .params = ib_params, .param_count = 3,
    .state_size = sizeof(IirBankState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = false
};

static const OrpheusComponentDescriptor* ib_get_descriptor(void) { return &ib_descriptor; }

/* ---- 生命周期 ---- */

static int ib_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(IirBankState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}

static int ib_destroy(void* state) { (void)state; return ORPHEUS_OK; }

static int ib_prepare(void* state, const OrpheusConfig* config) {
    IirBankState* s = (IirBankState*)state;
    s->channels  = config->channels > 0 ? config->channels : 2;
    s->numStages = (uint32_t)read_int(config, "num_stages", 4);
    if (s->numStages < 1) s->numStages = 1;
    if (s->numStages > IIR_BANK_MAX_STAGES) s->numStages = IIR_BANK_MAX_STAGES;

    /* 初始化系数为 unity（b0=1, b1=b2=a1=a2=0，即直通） */
    for (uint32_t i = 0; i < IIR_BANK_MAX_STAGES; ++i) {
        s->coefs[5*i + 0] = 1.0f;
        s->coefs[5*i + 1] = 0.0f;
        s->coefs[5*i + 2] = 0.0f;
        s->coefs[5*i + 3] = 0.0f;
        s->coefs[5*i + 4] = 0.0f;
    }

    /* 清零滤波器状态 */
    for (uint32_t i = 0; i < IIR_BANK_MAX_STAGES; ++i) {
        memset(s->stages[i].z1, 0, sizeof(s->stages[i].z1));
        memset(s->stages[i].z2, 0, sizeof(s->stages[i].z2));
    }
    return ORPHEUS_OK;
}

static int ib_reset(void* state) {
    IirBankState* s = (IirBankState*)state;
    for (uint32_t i = 0; i < IIR_BANK_MAX_STAGES; ++i) {
        memset(s->stages[i].z1, 0, sizeof(s->stages[i].z1));
        memset(s->stages[i].z2, 0, sizeof(s->stages[i].z2));
    }
    return ORPHEUS_OK;
}

/* ---- 实时处理 ---- */

static int ib_process(void* state, const OrpheusProcessContext* ctx) {
    IirBankState* s = (IirBankState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (in == NULL || out == NULL) return ORPHEUS_ERR_INVALID_ARG;

    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    uint32_t ns = s->numStages;
    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;
    const float* c = s->coefs;  /* 连续系数数组（BULK 双缓冲前缓冲） */

    /* 各通道独立级联：x -> stage[0] -> stage[1] -> ... -> out */
    for (uint32_t n = 0; n < frames; ++n) {
        for (uint32_t cc = 0; cc < ch; ++cc) {
            float x = in_data[n * ch + cc];
            for (uint32_t i = 0; i < ns; ++i) {
                float b0 = c[5*i + 0], b1 = c[5*i + 1], b2 = c[5*i + 2];
                float a1 = c[5*i + 3], a2 = c[5*i + 4];
                IirStageState* st = &s->stages[i];
                float z1 = st->z1[cc];
                float z2 = st->z2[cc];
                float y = b0 * x + b1 * z1 + b2 * z2 - a1 * z1 - a2 * z2;
                st->z2[cc] = z1;
                st->z1[cc] = y;
                x = y;
            }
            out_data[n * ch + cc] = x;
        }
    }
    out->frame_count = frames;
    return ORPHEUS_OK;
}

/* ---- 参数 ---- */

static int ib_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    (void)state; (void)param_id; (void)value;
    return ORPHEUS_ERR_UNSUPPORTED;  /* 全部 restart_required */
}

static int ib_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    IirBankState* s = (IirBankState*)state;
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->channels; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "num_stages") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->numStages; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

/* ---- 槽注册 ---- */

static int ib_register_slots(void* state, const OrpheusRegistry* reg) {
    IirBankState* s = (IirBankState*)state;
    /* 单个 BULK 槽：连续系数数组，count = 5 × MAX_STAGES（实际用 5×numStages） */
    OrpheusSlotInfo coefs_slot = {
        .kind = ORPHEUS_SLOT_BULK, .key = "coefs", .name = "IIR 系数",
        .type = ORPHEUS_VALUE_FLOAT,
        .offset = (size_t)((char*)&s->coefs[0] - (char*)s),
        .size = sizeof(float), .count = 5 * IIR_BANK_MAX_STAGES,
        .flags = ORPHEUS_SLOT_DOUBLE_BUFFERED
    };
    reg->add(reg->ctx, &coefs_slot);
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, numStages, ORPHEUS_SLOT_SETTING, "num_stages", "级数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=16,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    return ORPHEUS_OK;
}

/* ---- 接口 ---- */

static const OrpheusComponentInterface ib_interface = {
    .get_descriptor = ib_get_descriptor, .create = ib_create, .destroy = ib_destroy,
    .prepare = ib_prepare, .reset = ib_reset, .process = ib_process,
    .set_parameter = ib_set_parameter, .get_parameter = ib_get_parameter,
    .get_state_value = NULL, .register_slots = ib_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &ib_interface; }
