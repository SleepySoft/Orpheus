#include "orpheus_ifft.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI_F 3.14159265358979f

/* ---- radix-2 迭代 FFT（与 rfft 相同实现） ---- */

static void bit_reverse(float* re, float* im, uint32_t n) {
    uint32_t j = 0;
    for (uint32_t i = 0; i < n - 1; ++i) {
        if (i < j) {
            float tr = re[i]; re[i] = re[j]; re[j] = tr;
            float ti = im[i]; im[i] = im[j]; im[j] = ti;
        }
        uint32_t k = n >> 1;
        while (k <= j) { j -= k; k >>= 1; }
        j += k;
    }
}

static void fft_forward(float* re, float* im, uint32_t n,
                         const float* cosT, const float* sinT) {
    bit_reverse(re, im, n);
    for (uint32_t s = 1; s < n; s <<= 1) {
        uint32_t step = n / (2 * s);
        for (uint32_t j = 0; j < s; ++j) {
            uint32_t twIdx = j * step;
            float c = cosT[twIdx];
            float sn = sinT[twIdx];
            for (uint32_t k = j; k < n; k += 2 * s) {
                uint32_t idx = k + s;
                float tr = c * re[idx] - sn * im[idx];
                float ti = c * im[idx] + sn * re[idx];
                re[idx] = re[k] - tr;
                im[idx] = im[k] - ti;
                re[k]  += tr;
                im[k]  += ti;
            }
        }
    }
}

/* ---- 描述符 ---- */

static const OrpheusParameter ifft_params[] = {
    { .id = "channels", .name = "Channels", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32,
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true }
};

static const OrpheusPort ifft_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor ifft_descriptor = {
    .id = "orpheus.builtin.ifft", .version = "1.0.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = ifft_ports, .port_count = 2, .params = ifft_params, .param_count = 1,
    .state_size = sizeof(IfftState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = false
};

static const OrpheusComponentDescriptor* ifft_get_descriptor(void) { return &ifft_descriptor; }

/* ---- 生命周期 ---- */

static int ifft_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(IfftState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}

static int ifft_destroy(void* state) { (void)state; return ORPHEUS_OK; }

static int ifft_prepare(void* state, const OrpheusConfig* config) {
    IfftState* s = (IfftState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    s->fftSize  = config->block_size > 0 ? config->block_size : 256;

    uint32_t n = s->fftSize;
    if (n < 4 || n > IFFT_MAX_SIZE || (n & (n - 1)) != 0) {
        return ORPHEUS_ERR_INVALID_ARG;
    }
    for (uint32_t k = 0; k < n / 2; ++k) {
        float angle = -2.0f * PI_F * (float)k / (float)n;
        s->twiddleCos[k] = cosf(angle);
        s->twiddleSin[k] = sinf(angle);
    }
    return ORPHEUS_OK;
}

static int ifft_reset(void* state) { (void)state; return ORPHEUS_OK; }

/* ---- 实时处理 ---- */

static int ifft_process(void* state, const OrpheusProcessContext* ctx) {
    IfftState* s = (IfftState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (in == NULL || out == NULL) return ORPHEUS_ERR_INVALID_ARG;

    uint32_t n = s->fftSize;
    uint32_t ch = s->channels;
    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;

    for (uint32_t c = 0; c < ch; ++c) {
        /* 解包半复数格式为完整复数（Hermitian 对称）:
           R(0) = hc[0], I(0) = 0
           R(k) = hc[k], I(k) = hc[N-k]          for k = 1..N/2-1
           R(N/2) = hc[N/2], I(N/2) = 0
           R(N-k) = R(k), I(N-k) = -I(k)          [Hermitian] */
        s->scratchR[0] = in_data[0 * ch + c];
        s->scratchI[0] = 0.0f;
        for (uint32_t k = 1; k < n / 2; ++k) {
            s->scratchR[k] = in_data[k * ch + c];
            s->scratchI[k] = in_data[(n - k) * ch + c];
            s->scratchR[n - k] = s->scratchR[k];
            s->scratchI[n - k] = -s->scratchI[k];
        }
        s->scratchR[n / 2] = in_data[(n / 2) * ch + c];
        s->scratchI[n / 2] = 0.0f;

        /* 逆 FFT: IFFT(X) = (1/N) * conj(FFT(conj(X))) */
        /* conj(X): 取反虚部 */
        for (uint32_t k = 0; k < n; ++k) s->scratchI[k] = -s->scratchI[k];
        fft_forward(s->scratchR, s->scratchI, n, s->twiddleCos, s->twiddleSin);
        /* conj(result) / N: 取反虚部并缩放 */
        float invN = 1.0f / (float)n;
        for (uint32_t k = 0; k < n; ++k) {
            s->scratchR[k] *= invN;
            s->scratchI[k] = -s->scratchI[k] * invN;
        }

        /* 输出实部 */
        for (uint32_t k = 0; k < n; ++k) {
            out_data[k * ch + c] = s->scratchR[k];
        }
    }
    out->frame_count = n;
    return ORPHEUS_OK;
}

/* ---- 参数 ---- */

static int ifft_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    (void)state; (void)param_id; (void)value;
    return ORPHEUS_ERR_UNSUPPORTED;
}

static int ifft_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    IfftState* s = (IfftState*)state;
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->channels; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

/* ---- 槽注册 ---- */

static int ifft_register_slots(void* state, const OrpheusRegistry* reg) {
    IfftState* s = (IfftState*)state;
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    return ORPHEUS_OK;
}

/* ---- 接口 ---- */

static const OrpheusComponentInterface ifft_interface = {
    .get_descriptor = ifft_get_descriptor, .create = ifft_create, .destroy = ifft_destroy,
    .prepare = ifft_prepare, .reset = ifft_reset, .process = ifft_process,
    .set_parameter = ifft_set_parameter, .get_parameter = ifft_get_parameter,
    .get_state_value = NULL, .register_slots = ifft_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &ifft_interface; }
