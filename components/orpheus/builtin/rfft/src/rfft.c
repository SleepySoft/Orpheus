#include "orpheus_rfft.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI_F 3.14159265358979f

/* ---- radix-2 迭代 FFT ---- */

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

/* 正向复数 FFT（就地），twiddle 表为 exp(-2*pi*i*k/N) */
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

static const OrpheusParameter rfft_params[] = {
    { .id = "channels", .name = "Channels", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 2 },
      .min_i32 = 1, .max_i32 = 32,
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true }
};

static const OrpheusPort rfft_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor rfft_descriptor = {
    .id = "orpheus.builtin.rfft", .version = "1.0.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = rfft_ports, .port_count = 2, .params = rfft_params, .param_count = 1,
    .state_size = sizeof(RfftState), .scratch_size = 0, .alignment = 8,
    .latency_samples = 0, .realtime_safe = true, .supports_inplace = false
};

static const OrpheusComponentDescriptor* rfft_get_descriptor(void) { return &rfft_descriptor; }

/* ---- 生命周期 ---- */

static int rfft_create(void** state, const OrpheusConfig* config) {
    if (config != NULL && config->state_block != NULL) {
        *state = config->state_block;
        return ORPHEUS_OK;
    }
    *state = calloc(1, sizeof(RfftState));
    return *state ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}

static int rfft_destroy(void* state) { (void)state; return ORPHEUS_OK; }

static int rfft_prepare(void* state, const OrpheusConfig* config) {
    RfftState* s = (RfftState*)state;
    s->channels = config->channels > 0 ? config->channels : 2;
    s->fftSize  = config->block_size > 0 ? config->block_size : 256;

    /* 验证 2 的幂且在范围内 */
    uint32_t n = s->fftSize;
    if (n < 4 || n > RFFT_MAX_SIZE || (n & (n - 1)) != 0) {
        return ORPHEUS_ERR_INVALID_ARG;
    }

    /* 预计算 twiddle 因子: W[k] = exp(-2*pi*i*k/N) */
    for (uint32_t k = 0; k < n / 2; ++k) {
        float angle = -2.0f * PI_F * (float)k / (float)n;
        s->twiddleCos[k] = cosf(angle);
        s->twiddleSin[k] = sinf(angle);
    }
    return ORPHEUS_OK;
}

static int rfft_reset(void* state) { (void)state; return ORPHEUS_OK; }

/* ---- 实时处理 ---- */

static int rfft_process(void* state, const OrpheusProcessContext* ctx) {
    RfftState* s = (RfftState*)state;
    const OrpheusBuffer* in = ctx->inputs[0];
    OrpheusBuffer* out = ctx->outputs[0];
    if (in == NULL || out == NULL) return ORPHEUS_ERR_INVALID_ARG;

    uint32_t n = s->fftSize;
    uint32_t ch = s->channels;
    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;

    for (uint32_t c = 0; c < ch; ++c) {
        /* 复制实数输入到暂存，虚部清零 */
        for (uint32_t k = 0; k < n; ++k) {
            s->scratchR[k] = in_data[k * ch + c];
            s->scratchI[k] = 0.0f;
        }

        /* 正向 FFT */
        fft_forward(s->scratchR, s->scratchI, n, s->twiddleCos, s->twiddleSin);

        /* 打包为半复数格式:
           hc[0]     = R(0)        [DC]
           hc[1..N/2-1] = R(k)
           hc[N/2]   = R(N/2)      [Nyquist]
           hc[N/2+1..N-1] = I(N-k)  [虚部逆序] */
        out_data[0 * ch + c] = s->scratchR[0];
        for (uint32_t k = 1; k < n / 2; ++k) {
            out_data[k * ch + c] = s->scratchR[k];
        }
        out_data[(n / 2) * ch + c] = s->scratchR[n / 2];
        for (uint32_t k = 1; k < n / 2; ++k) {
            out_data[(n - k) * ch + c] = s->scratchI[k];
        }
    }
    out->frame_count = n;
    return ORPHEUS_OK;
}

/* ---- 参数 ---- */

static int rfft_set_parameter(void* state, const char* param_id, const OrpheusValue* value) {
    (void)state; (void)param_id; (void)value;
    return ORPHEUS_ERR_UNSUPPORTED;
}

static int rfft_get_parameter(void* state, const char* param_id, OrpheusValue* value) {
    RfftState* s = (RfftState*)state;
    if (strcmp(param_id, "channels") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->channels; return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

/* ---- 槽注册 ---- */

static int rfft_register_slots(void* state, const OrpheusRegistry* reg) {
    RfftState* s = (RfftState*)state;
    ORPHEUS_REG_SLOT(reg, s, channels, ORPHEUS_SLOT_SETTING, "channels", "通道数",
                     ORPHEUS_VALUE_INT, .min_i32=1, .max_i32=32,
                     .update_policy=ORPHEUS_UPDATE_RESTART_REQUIRED,
                     .flags=ORPHEUS_SLOT_PERSISTENT | ORPHEUS_SLOT_READBACK |
                            ORPHEUS_SLOT_AFFECTS_SIGNATURE);
    return ORPHEUS_OK;
}

/* ---- 接口 ---- */

static const OrpheusComponentInterface rfft_interface = {
    .get_descriptor = rfft_get_descriptor, .create = rfft_create, .destroy = rfft_destroy,
    .prepare = rfft_prepare, .reset = rfft_reset, .process = rfft_process,
    .set_parameter = rfft_set_parameter, .get_parameter = rfft_get_parameter,
    .get_state_value = NULL, .register_slots = rfft_register_slots
};

#ifndef ORPHEUS_ENTRY_NAME
#define ORPHEUS_ENTRY_NAME orpheus_get_interface
#endif
ORPHEUS_API const OrpheusComponentInterface* ORPHEUS_ENTRY_NAME(void) { return &rfft_interface; }
