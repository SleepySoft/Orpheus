#include "orpheus_nlms.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static float read_float(const OrpheusConfig* config, const char* id, float fallback) {
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], id) == 0) {
            if (config->param_values[i].type == ORPHEUS_VALUE_FLOAT) return config->param_values[i].value.f32;
            if (config->param_values[i].type == ORPHEUS_VALUE_INT) return (float)config->param_values[i].value.i32;
        }
    }
    return fallback;
}

static const OrpheusParameter nlms_params[] = {
    { .id = "channels", .name = "通道数", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 64, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "filter_length", .name = "滤波器长度", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 32 },
      .min_i32 = 1, .max_i32 = 4096, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "step_size", .name = "步长", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.01f },
      .min_f32 = 0.0f, .max_f32 = 2.0f, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "leakage", .name = "泄漏因子", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 1.0f },
      .min_f32 = 0.0f, .max_f32 = 1.0f, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = false },
    { .id = "eps", .name = "正则化", .type = ORPHEUS_VALUE_FLOAT,
      .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 1e-6f },
      .min_f32 = 1e-12f, .max_f32 = 1.0f, .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = false }
};

static const OrpheusPort nlms_ports[] = {
    { .id = "ref", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" },
    { .id = "err", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor nlms_descriptor = {
    .id = "orpheus.builtin.nlms", .version = "1.0.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = nlms_ports, .port_count = 3, .params = nlms_params, .param_count = 5,
    .state_size = sizeof(NlmsState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = false
};

static const OrpheusComponentDescriptor* nlms_get_descriptor(void) { return &nlms_descriptor; }

static void nlms_free(NlmsState* s) {
    free(s->w);
    free(s->x);
    free(s->pos);
    s->w = NULL;
    s->x = NULL;
    s->pos = NULL;
}

static int nlms_create(void** state, const OrpheusConfig* config) {
    (void)config;
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(NlmsState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}

static int nlms_destroy(void* state) {
    nlms_free((NlmsState*)state);
    return ORPHEUS_OK;
}

static int nlms_prepare(void* state, const OrpheusConfig* config) {
    NlmsState* s = (NlmsState*)state;
    nlms_free(s);

    s->channels = config->channels > 0 ? config->channels : 2;
    s->filter_length = (uint32_t)read_float(config, "filter_length", 32.0f);
    if (s->filter_length < 1) s->filter_length = 32;
    if (s->filter_length > 4096) s->filter_length = 4096;
    s->mu = read_float(config, "step_size", 0.01f);
    s->leak = read_float(config, "leakage", 1.0f);
    s->eps = read_float(config, "eps", 1e-6f);
    if (s->eps < 1e-12f) s->eps = 1e-12f;

    s->w = (float*)calloc((size_t)s->channels * s->filter_length, sizeof(float));
    s->x = (float*)calloc((size_t)s->channels * s->filter_length, sizeof(float));
    s->pos = (uint32_t*)calloc(s->channels, sizeof(uint32_t));
    if (!s->w || !s->x || !s->pos) {
        nlms_free(s);
        return ORPHEUS_ERR_OUT_OF_MEMORY;
    }
    return ORPHEUS_OK;
}

static int nlms_reset(void* state) {
    NlmsState* s = (NlmsState*)state;
    if (s->w) memset(s->w, 0, (size_t)s->channels * s->filter_length * sizeof(float));
    if (s->x) memset(s->x, 0, (size_t)s->channels * s->filter_length * sizeof(float));
    if (s->pos) memset(s->pos, 0, s->channels * sizeof(uint32_t));
    return ORPHEUS_OK;
}

static int nlms_process(void* state, const OrpheusProcessContext* ctx) {
    NlmsState* s = (NlmsState*)state;
    const OrpheusBuffer* ref_buf = ctx->inputs[0];
    const OrpheusBuffer* err_buf = ctx->inputs[1];
    OrpheusBuffer* out_buf = ctx->outputs[0];
    if (ref_buf == NULL || out_buf == NULL) return ORPHEUS_ERR_INVALID_ARG;

    uint32_t ch = s->channels;
    uint32_t L = s->filter_length;
    float mu = s->mu;
    float leak = s->leak;
    float eps = s->eps;
    const float* ref = (const float*)ref_buf->data;
    const float* err = err_buf ? (const float*)err_buf->data : NULL;
    float* out = (float*)out_buf->data;
    uint32_t frames = ctx->frame_count;

    for (uint32_t c = 0; c < ch; ++c) {
        float* wc = s->w + (size_t)c * L;
        float* xc = s->x + (size_t)c * L;
        uint32_t p = s->pos[c];
        for (uint32_t n = 0; n < frames; ++n) {
            xc[p] = ref[n * ch + c];
            /* 计算输出 y = w^T x */
            float y = 0.0f;
            float norm = 0.0f;
            for (uint32_t i = 0; i < L; ++i) {
                uint32_t idx = (p + L - i) % L;
                float xv = xc[idx];
                y += wc[i] * xv;
                norm += xv * xv;
            }
            out[n * ch + c] = y;
            /* 若提供误差，则更新系数 */
            if (err != NULL) {
                float e = err[n * ch + c];
                float scale = mu * e / (norm + eps);
                for (uint32_t i = 0; i < L; ++i) {
                    uint32_t idx = (p + L - i) % L;
                    wc[i] = leak * wc[i] + scale * xc[idx];
                }
            }
            p = (p + 1) % L;
        }
        s->pos[c] = p;
    }

    out_buf->frame_count = frames;
    return ORPHEUS_OK;
}

static int nlms_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    NlmsState* s = (NlmsState*)state;
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->channels; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "filter_length") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->filter_length; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "step_size") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT; value->value.f32 = s->mu; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "leakage") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT; value->value.f32 = s->leak; return ORPHEUS_OK;
    }
    if (strcmp(param_id, "eps") == 0) {
        value->type = ORPHEUS_VALUE_FLOAT; value->value.f32 = s->eps; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int nlms_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    (void)state; (void)param_id; (void)value;
    return ORPHEUS_ERR_UNSUPPORTED;
}

static int nlms_register_slots(void* state, const OrpheusRegistry* reg) {
    NlmsState* s = (NlmsState*)state;
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=64,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, filter_length, ORPHEUS_SLOT_SETTING, "filter_length", "滤波器长度",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=4096,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK | ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    ORPHEUS_REG_SLOT(reg, s, mu, ORPHEUS_SLOT_SETTING, "step_size", "步长",
                     ORPHEUS_VALUE_FLOAT, .min_f32=0.0f, .max_f32=2.0f,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, leak, ORPHEUS_SLOT_SETTING, "leakage", "泄漏因子",
                     ORPHEUS_VALUE_FLOAT, .min_f32=0.0f, .max_f32=1.0f,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, eps, ORPHEUS_SLOT_SETTING, "eps", "正则化",
                     ORPHEUS_VALUE_FLOAT, .min_f32=1e-12f, .max_f32=1.0f,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface nlms_interface = {
    .get_descriptor = nlms_get_descriptor, .create = nlms_create, .destroy = nlms_destroy,
    .prepare = nlms_prepare, .reset = nlms_reset, .process = nlms_process,
    .set_parameter = nlms_set_parameter, .get_parameter = nlms_get_parameter,
    .get_state_value = NULL, .register_slots = nlms_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &nlms_interface; }
