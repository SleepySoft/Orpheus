#include "component_state.h"
#include "component_api.h"
#include <math.h>

#define PI_F 3.14159265358979f

int orpheus_builtin_ifft_normalize(orpheus_builtin_ifft_Parameters* p, const char* key) {
    (void)key;
    return p->fft_size >= 4 && !(p->fft_size & (p->fft_size - 1))
        ? ORPHEUS_OK : ORPHEUS_ERR_INVALID_ARG;
}

int orpheus_builtin_ifft_prepare(orpheus_builtin_ifft_State* s, const OrpheusConfig* config) {
    (void)config;
    for (uint32_t k = 0; k < (uint32_t)s->fft_size / 2; ++k) {
        float angle = -2.0f * PI_F * (float)k / (float)s->fft_size;
        s->algorithm.twiddleCos[k] = cosf(angle);
        s->algorithm.twiddleSin[k] = sinf(angle);
    }
    return ORPHEUS_OK;
}

static void fft_forward(float* re, float* im, uint32_t n, const float* cosT, const float* sinT) {
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
    for (uint32_t span = 1; span < n; span <<= 1) {
        uint32_t step = n / (2 * span);
        for (uint32_t a = 0; a < span; ++a) {
            float c = cosT[a * step], sn = sinT[a * step];
            for (uint32_t k = a; k < n; k += 2 * span) {
                uint32_t idx = k + span;
                float tr = c * re[idx] - sn * im[idx];
                float ti = c * im[idx] + sn * re[idx];
                re[idx] = re[k] - tr;
                im[idx] = im[k] - ti;
                re[k] += tr;
                im[k] += ti;
            }
        }
    }
}

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return na && nb && (x >= y ? x - y < nb : y - x < na);
}

int orpheus_builtin_ifft_algorithm(orpheus_builtin_ifft_State* s,
    orpheus_builtin_ifft_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if ((io->in.buffer == NULL) == (io->spectrum.buffer == NULL)) return ORPHEUS_ERR_INVALID_ARG;
    const float* samples = io->spectrum.buffer ? io->spectrum.samples : io->in.samples;
    uint32_t input = io->spectrum.buffer ? io->spectrum.input_index : io->in.input_index;
    if (ctx->trigger_input != input) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t n = (uint32_t)s->fft_size, channels = (uint32_t)s->channels;
    uint32_t frames = n * (uint32_t)s->batches;
    size_t bytes = (size_t)frames * channels * sizeof(float);
    if (overlap(samples, bytes, s, sizeof(*s)) || (io->out.samples &&
        (overlap(io->out.samples, bytes, samples, bytes) || overlap(io->out.samples, bytes, s, sizeof(*s)))))
        return ORPHEUS_ERR_INVALID_ARG;
    for (uint32_t frame = 0; frame < frames; frame += n) {
        for (uint32_t ch = 0; ch < channels; ++ch) {
            const float* src = samples + (size_t)frame * channels + ch;
            float* re = s->algorithm.scratchR;
            float* im = s->algorithm.scratchI;
            re[0] = src[0]; im[0] = 0.0f;
            for (uint32_t k = 1; k < n / 2; ++k) {
                re[k] = src[k * channels]; im[k] = src[(n - k) * channels];
                re[n - k] = re[k]; im[n - k] = -im[k];
            }
            re[n / 2] = src[n / 2 * channels]; im[n / 2] = 0.0f;
            for (uint32_t k = 0; k < n; ++k) im[k] = -im[k];
            fft_forward(re, im, n, s->algorithm.twiddleCos, s->algorithm.twiddleSin);
            float invN = 1.0f / (float)n;
            for (uint32_t k = 0; k < n; ++k) {
                re[k] *= invN;
                im[k] = -im[k] * invN;
                if (io->out.samples) io->out.samples[(size_t)(frame + k) * channels + ch] = re[k];
            }
        }
    }
    return orpheus_builtin_ifft_emit_out(ctx, &io->out);
}
