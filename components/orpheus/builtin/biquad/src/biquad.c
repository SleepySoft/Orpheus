#include "orpheus_biquad.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static const OrpheusParameter biquad_params[] = {
    {
        .id = "type",
        .name = "Filter Type",
        .type = ORPHEUS_VALUE_STRING,
        .default_value = { .type = ORPHEUS_VALUE_STRING, .value.str = "lowpass" },
        .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
        .readback = true,
        .persistent = true,
        .affects_signature = false
    },
    {
        .id = "fc",
        .name = "Cutoff Frequency",
        .type = ORPHEUS_VALUE_FLOAT,
        .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 1000.0f },
        .min_f32 = 20.0f,
        .max_f32 = 20000.0f,
        .unit = "Hz",
        .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
        .readback = true,
        .persistent = true,
        .affects_signature = false
    },
    {
        .id = "q",
        .name = "Q",
        .type = ORPHEUS_VALUE_FLOAT,
        .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.707f },
        .min_f32 = 0.1f,
        .max_f32 = 10.0f,
        .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
        .readback = true,
        .persistent = true,
        .affects_signature = false
    },
    {
        .id = "gain_db",
        .name = "Gain",
        .type = ORPHEUS_VALUE_FLOAT,
        .default_value = { .type = ORPHEUS_VALUE_FLOAT, .value.f32 = 0.0f },
        .min_f32 = -24.0f,
        .max_f32 = 24.0f,
        .unit = "dB",
        .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
        .readback = true,
        .persistent = true,
        .affects_signature = false
    },
    {
        .id = "channels",
        .name = "Channels",
        .type = ORPHEUS_VALUE_INT,
        .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
        .min_i32 = 1,
        .max_i32 = 32,
        .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
        .readback = true,
        .persistent = true,
        .affects_signature = true
    },
    {
        .id = "form",
        .name = "Structure",
        .type = ORPHEUS_VALUE_STRING,
        .default_value = { .type = ORPHEUS_VALUE_STRING, .value.str = "df2t" },
        .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
        .readback = true,
        .persistent = true,
        .affects_signature = false
    }
};

static const OrpheusPort biquad_ports[] = {
    {
        .id = "in",
        .direction = ORPHEUS_PORT_INPUT,
        .type = ORPHEUS_PORT_AUDIO,
        .sample_format = ORPHEUS_FORMAT_F32,
        .channels = 0,
        .sample_rate = 0,
        .block_size = 0,
        .is_variable = true,
        .channels_param = "channels"
    },
    {
        .id = "out",
        .direction = ORPHEUS_PORT_OUTPUT,
        .type = ORPHEUS_PORT_AUDIO,
        .sample_format = ORPHEUS_FORMAT_F32,
        .channels = 0,
        .sample_rate = 0,
        .block_size = 0,
        .is_variable = true,
        .channels_param = "channels"
    }
};

static const OrpheusComponentDescriptor biquad_descriptor = {
    .id = "orpheus.builtin.biquad",
    .version = "1.1.0",
    .abi_version = ORPHEUS_ABI_VERSION,
    .ports = biquad_ports,
    .port_count = 2,
    .params = biquad_params,
    .param_count = 6,
    .state_size = sizeof(BiquadState),
    .scratch_size = 0,
    .alignment = 8,
    .latency_samples = 0,
    .realtime_safe = true,
    .supports_inplace = true
};

static void compute_coeffs(const char* type, float fc, float q, float gain_db, float sample_rate,
                           float* b0, float* b1, float* b2, float* a1, float* a2) {
    float A = powf(10.0f, gain_db / 40.0f);
    float w0 = 2.0f * 3.14159265358979f * fc / sample_rate;
    float cosw0 = cosf(w0);
    float sinw0 = sinf(w0);
    float alpha = sinw0 / (2.0f * q);

    *b0 = *b1 = *b2 = *a1 = *a2 = 0.0f;

    if (strcmp(type, "lowpass") == 0) {
        *b0 = (1.0f - cosw0) / 2.0f;
        *b1 = 1.0f - cosw0;
        *b2 = (1.0f - cosw0) / 2.0f;
        float a0 = 1.0f + alpha;
        *a1 = -2.0f * cosw0 / a0;
        *a2 = (1.0f - alpha) / a0;
        *b0 /= a0; *b1 /= a0; *b2 /= a0;
    } else if (strcmp(type, "highpass") == 0) {
        *b0 = (1.0f + cosw0) / 2.0f;
        *b1 = -(1.0f + cosw0);
        *b2 = (1.0f + cosw0) / 2.0f;
        float a0 = 1.0f + alpha;
        *a1 = -2.0f * cosw0 / a0;
        *a2 = (1.0f - alpha) / a0;
        *b0 /= a0; *b1 /= a0; *b2 /= a0;
    } else if (strcmp(type, "bandpass") == 0) {
        *b0 = alpha;
        *b1 = 0.0f;
        *b2 = -alpha;
        float a0 = 1.0f + alpha;
        *a1 = -2.0f * cosw0 / a0;
        *a2 = (1.0f - alpha) / a0;
        *b0 /= a0; *b1 /= a0; *b2 /= a0;
    } else if (strcmp(type, "notch") == 0) {
        *b0 = 1.0f;
        *b1 = -2.0f * cosw0;
        *b2 = 1.0f;
        float a0 = 1.0f + alpha;
        *a1 = -2.0f * cosw0 / a0;
        *a2 = (1.0f - alpha) / a0;
        *b0 /= a0; *b1 /= a0; *b2 /= a0;
    } else if (strcmp(type, "peaking") == 0) {
        *b0 = 1.0f + alpha * A;
        *b1 = -2.0f * cosw0;
        *b2 = 1.0f - alpha * A;
        float a0 = 1.0f + alpha / A;
        *a1 = -2.0f * cosw0 / a0;
        *a2 = (1.0f - alpha / A) / a0;
        *b0 /= a0; *b1 /= a0; *b2 /= a0;
    } else if (strcmp(type, "lowshelf") == 0) {
        float sqrtA = sqrtf(A);
        *b0 = A * ((A + 1.0f) - (A - 1.0f) * cosw0 + 2.0f * sqrtA * alpha);
        *b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw0);
        *b2 = A * ((A + 1.0f) - (A - 1.0f) * cosw0 - 2.0f * sqrtA * alpha);
        float a0 = (A + 1.0f) + (A - 1.0f) * cosw0 + 2.0f * sqrtA * alpha;
        *a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosw0) / a0;
        *a2 = ((A + 1.0f) + (A - 1.0f) * cosw0 - 2.0f * sqrtA * alpha) / a0;
        *b0 /= a0; *b1 /= a0; *b2 /= a0;
    } else if (strcmp(type, "highshelf") == 0) {
        float sqrtA = sqrtf(A);
        *b0 = A * ((A + 1.0f) + (A - 1.0f) * cosw0 + 2.0f * sqrtA * alpha);
        *b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw0);
        *b2 = A * ((A + 1.0f) + (A - 1.0f) * cosw0 - 2.0f * sqrtA * alpha);
        float a0 = (A + 1.0f) - (A - 1.0f) * cosw0 + 2.0f * sqrtA * alpha;
        *a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cosw0) / a0;
        *a2 = ((A + 1.0f) - (A - 1.0f) * cosw0 - 2.0f * sqrtA * alpha) / a0;
        *b0 /= a0; *b1 /= a0; *b2 /= a0;
    } else {
        /* default: lowpass */
        *b0 = (1.0f - cosw0) / 2.0f;
        *b1 = 1.0f - cosw0;
        *b2 = (1.0f - cosw0) / 2.0f;
        float a0 = 1.0f + alpha;
        *a1 = -2.0f * cosw0 / a0;
        *a2 = (1.0f - alpha) / a0;
        *b0 /= a0; *b1 /= a0; *b2 /= a0;
    }
}

static const OrpheusComponentDescriptor* biquad_get_descriptor(void) {
    return &biquad_descriptor;
}

static int biquad_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(BiquadState));
    if (*state == NULL) return ORPHEUS_ERR_OUT_OF_MEMORY;
    return ORPHEUS_OK;
}

static int biquad_destroy(void* state) {
    (void)state; /* v2：内存由 Runtime 统一管理 */
    return ORPHEUS_OK;
}

static int biquad_prepare(void* state, const OrpheusConfig* config) {
    BiquadState* s = (BiquadState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;

    const char* type = "lowpass";
    const char* form = "df2t";
    float fc = 1000.0f;
    float q = 0.707f;
    float gain_db = 0.0f;

    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (!config->param_ids[i]) continue;
        if (strcmp(config->param_ids[i], "type") == 0 && config->param_values[i].type == ORPHEUS_VALUE_STRING) {
            type = config->param_values[i].value.str;
        } else if (strcmp(config->param_ids[i], "form") == 0 && config->param_values[i].type == ORPHEUS_VALUE_STRING) {
            form = config->param_values[i].value.str;
        } else if (strcmp(config->param_ids[i], "fc") == 0 && config->param_values[i].type == ORPHEUS_VALUE_FLOAT) {
            fc = config->param_values[i].value.f32;
        } else if (strcmp(config->param_ids[i], "q") == 0 && config->param_values[i].type == ORPHEUS_VALUE_FLOAT) {
            q = config->param_values[i].value.f32;
        } else if (strcmp(config->param_ids[i], "gain_db") == 0 && config->param_values[i].type == ORPHEUS_VALUE_FLOAT) {
            gain_db = config->param_values[i].value.f32;
        }
    }

    compute_coeffs(type, fc, q, gain_db, (float)config->sample_rate,
                   &s->b0, &s->b1, &s->b2, &s->a1, &s->a2);
    strncpy(s->type, type, sizeof(s->type) - 1);
    s->type[sizeof(s->type) - 1] = '\0';
    /* 结构选择：df1=传统直接 I 型；其他（含未知值）回退 df2t（直接 II 型转置，滚动） */
    if (form != NULL && strcmp(form, "df1") == 0) {
        strncpy(s->form, "df1", sizeof(s->form) - 1);
    } else {
        strncpy(s->form, "df2t", sizeof(s->form) - 1);
    }
    s->form[sizeof(s->form) - 1] = '\0';
    s->fc = fc;
    s->q = q;
    s->gain_db = gain_db;

    biquad_clear_history(s);

    return ORPHEUS_OK;
}

static int biquad_reset(void* state) {
    BiquadState* s = (BiquadState*)state;
    biquad_clear_history(s);
    return ORPHEUS_OK;
}

static int biquad_process(void* state, const OrpheusProcessContext* ctx) {
    BiquadState* s = (BiquadState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];

    if (in == NULL || out == NULL) return ORPHEUS_ERR_INVALID_ARG;

    uint32_t frames = ctx->frame_count;
    uint32_t ch = s->channels;
    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;

    for (uint32_t c = 0; c < ch; ++c) {
        for (uint32_t n = 0; n < frames; ++n) {
            out_data[n * ch + c] = biquad_tick(s, c, in_data[n * ch + c]);
        }
    }

    out->frame_count = frames;
    return ORPHEUS_OK;
}

static int biquad_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    (void)state; (void)param_id; (void)value;
    return ORPHEUS_ERR_UNSUPPORTED;
}

static int biquad_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    BiquadState* s = (BiquadState*)state;
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT;
        value->value.i32 = (int32_t)s->channels;
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

static int biquad_register_slots(void* state, const OrpheusRegistry* reg) {
    BiquadState* s = (BiquadState*)state;
    ORPHEUS_REG_SLOT(reg, s, type, ORPHEUS_SLOT_SETTING, "type", "滤波器类型",
                     ORPHEUS_VALUE_STRING, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, fc, ORPHEUS_SLOT_SETTING, "fc", "截止频率",
                     ORPHEUS_VALUE_FLOAT, .min_f32=20.0f, .max_f32=20000.0f, .unit="Hz",
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, q, ORPHEUS_SLOT_SETTING, "q", "Q",
                     ORPHEUS_VALUE_FLOAT, .min_f32=0.1f, .max_f32=10.0f,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, gain_db, ORPHEUS_SLOT_SETTING, "gain_db", "增益",
                     ORPHEUS_VALUE_FLOAT, .min_f32=-24.0f, .max_f32=24.0f, .unit="dB",
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, form, ORPHEUS_SLOT_SETTING, "form", "滤波结构",
                     ORPHEUS_VALUE_STRING, .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK);
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    return ORPHEUS_OK;
}

static const OrpheusComponentInterface biquad_interface = {
    .get_descriptor = biquad_get_descriptor,
    .create = biquad_create,
    .destroy = biquad_destroy,
    .prepare = biquad_prepare,
    .reset = biquad_reset,
    .process = biquad_process,
    .set_parameter = biquad_set_parameter,
    .get_parameter = biquad_get_parameter,
    .get_state_value = NULL,
    .register_slots = biquad_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) {
    return &biquad_interface;
}
