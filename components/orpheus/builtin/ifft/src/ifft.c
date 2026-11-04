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
                         const float* cosT, const float* sinT, uint32_t tableSize) {
    bit_reverse(re, im, n);
    for (uint32_t s = 1; s < n; s <<= 1) {
        uint32_t step = tableSize / (2 * s);
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
      .readback = true, .persistent = true, .affects_signature = true },
    { .id = "fft_size", .name = "FFT点数（0为单帧自动）", .type = ORPHEUS_VALUE_INT,
      .default_value = { .type = ORPHEUS_VALUE_INT, .value.i32 = 0 },
      .min_i32 = 0, .max_i32 = IFFT_MAX_SIZE,
      .update_policy = ORPHEUS_UPDATE_RESTART_REQUIRED,
      .readback = true, .persistent = true, .affects_signature = true }
};

static const OrpheusPort ifft_ports[] = {
    { .id = "in", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" },
    { .id = "spectrum", .direction = ORPHEUS_PORT_INPUT, .type = ORPHEUS_PORT_BULK,
      .sample_format = ORPHEUS_FORMAT_F32,
      .is_variable = true, .channels_param = "channels" },
    { .id = "out", .direction = ORPHEUS_PORT_OUTPUT, .type = ORPHEUS_PORT_AUDIO,
      .sample_format = ORPHEUS_FORMAT_F32, .channels = 0, .sample_rate = 0, .block_size = 0,
      .is_variable = true, .channels_param = "channels" }
};

static const OrpheusComponentDescriptor ifft_descriptor = {
    .id = "orpheus.builtin.ifft", .version = "1.1.0", .abi_version = ORPHEUS_ABI_VERSION,
    .ports = ifft_ports, .port_count = 3, .params = ifft_params, .param_count = 2,
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
    if (!s || !config) return ORPHEUS_ERR_INVALID_ARG;
    s->channels = config->channels > 0 ? config->channels : 2;
    s->fftSize = 0;
    for (uint32_t i = 0; i < config->param_count; ++i) {
        if (config->param_ids[i] && strcmp(config->param_ids[i], "fft_size") == 0) {
            if (config->param_values[i].type != ORPHEUS_VALUE_INT ||
                config->param_values[i].value.i32 < 0)
                return ORPHEUS_ERR_INVALID_ARG;
            s->fftSize = (uint32_t)config->param_values[i].value.i32;
        }
    }

    /* 自动单帧大小取实际输入buffer，而不是处理时间量子；实时阶段只选择表步长。 */
    uint32_t n = s->fftSize ? s->fftSize : IFFT_MAX_SIZE;
    if (s->channels > 32 || n < 4 || n > IFFT_MAX_SIZE || (n & (n - 1)) != 0) {
        return ORPHEUS_ERR_INVALID_ARG;
    }
    s->twiddleSize = n;
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
    if (!s || !ctx || !ctx->inputs || !ctx->outputs ||
        ctx->input_count < 1 || ctx->output_count < 1)
        return ORPHEUS_ERR_INVALID_ARG;
    const OrpheusBuffer* in = ctx->inputs[0];
    const OrpheusBuffer* spectrum = ctx->input_count > 1 ? ctx->inputs[1] : NULL;
    OrpheusBuffer* out = ctx->outputs[0];
    if ((in == NULL) == (spectrum == NULL) || !out) return ORPHEUS_ERR_INVALID_ARG;
    if (spectrum) in = spectrum;

    uint32_t frames = in->frame_count;
    uint32_t n = s->fftSize ? s->fftSize : frames;
    uint32_t ch = s->channels;
    if (n < 4 || n > IFFT_MAX_SIZE || (n & (n - 1)) != 0 ||
        s->twiddleSize < n || frames == 0 || frames % n != 0 ||
        !in->data || !out->data || in->format != ORPHEUS_FORMAT_F32 ||
        out->format != ORPHEUS_FORMAT_F32 || !in->interleaved || !out->interleaved ||
        in->channels != ch || out->channels != ch ||
        in->frame_capacity < frames || out->frame_capacity < frames)
        return ORPHEUS_ERR_INVALID_ARG;
    const float* in_data = (const float*)in->data;
    float* out_data = (float*)out->data;

    for (uint32_t frame = 0; frame < frames; frame += n) {
      for (uint32_t c = 0; c < ch; ++c) {
        /* 解包半复数格式为完整复数（Hermitian 对称）:
           R(0) = hc[0], I(0) = 0
           R(k) = hc[k], I(k) = hc[N-k]          for k = 1..N/2-1
           R(N/2) = hc[N/2], I(N/2) = 0
           R(N-k) = R(k), I(N-k) = -I(k)          [Hermitian] */
        const float* src = in_data + (size_t)frame * ch + c;
        float* dst = out_data + (size_t)frame * ch + c;
        s->scratchR[0] = src[0];
        s->scratchI[0] = 0.0f;
        for (uint32_t k = 1; k < n / 2; ++k) {
            s->scratchR[k] = src[k * ch];
            s->scratchI[k] = src[(n - k) * ch];
            s->scratchR[n - k] = s->scratchR[k];
            s->scratchI[n - k] = -s->scratchI[k];
        }
        s->scratchR[n / 2] = src[(n / 2) * ch];
        s->scratchI[n / 2] = 0.0f;

        /* 逆 FFT: IFFT(X) = (1/N) * conj(FFT(conj(X))) */
        /* conj(X): 取反虚部 */
        for (uint32_t k = 0; k < n; ++k) s->scratchI[k] = -s->scratchI[k];
        fft_forward(s->scratchR, s->scratchI, n, s->twiddleCos, s->twiddleSin,
                    s->twiddleSize);
        /* conj(result) / N: 取反虚部并缩放 */
        float invN = 1.0f / (float)n;
        for (uint32_t k = 0; k < n; ++k) {
            s->scratchR[k] *= invN;
            s->scratchI[k] = -s->scratchI[k] * invN;
        }

        /* 输出实部 */
        for (uint32_t k = 0; k < n; ++k) {
            dst[k * ch] = s->scratchR[k];
        }
      }
    }
    out->frame_count = frames;
    return orpheus_process_next(ctx);
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
    if (strcmp(param_id, "fft_size") == 0) {
        value->type = ORPHEUS_VALUE_INT; value->value.i32 = (int32_t)s->fftSize; return ORPHEUS_OK;
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
    ORPHEUS_REG_SLOT(reg, s, fftSize, ORPHEUS_SLOT_SETTING, "fft_size", "FFT点数（0为单帧自动）",
                     ORPHEUS_VALUE_INT, .min_i32=0, .max_i32=IFFT_MAX_SIZE,
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
