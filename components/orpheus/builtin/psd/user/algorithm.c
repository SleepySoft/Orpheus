#include "component_state.h"
#include "component_api.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static void fft_radix2(float* re, float* im, uint32_t n) {
    for (uint32_t i = 1, j = 0; i < n; ++i) {
        uint32_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            float t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (uint32_t len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * 3.14159265358979323846f / (float)len;
        float wr = cosf(ang), wi = sinf(ang);
        for (uint32_t i = 0; i < n; i += len) {
            float cwr = 1.0f, cwi = 0.0f;
            for (uint32_t k = 0; k < len / 2; ++k) {
                uint32_t a = i + k, b = i + k + len / 2;
                float tr = cwr * re[b] - cwi * im[b];
                float ti = cwr * im[b] + cwi * re[b];
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
                float nwr = cwr * wr - cwi * wi;
                cwi = cwr * wi + cwi * wr;
                cwr = nwr;
            }
        }
    }
}

int orpheus_builtin_psd_prepare(orpheus_builtin_psd_State* s, const OrpheusConfig* config) {
    uint32_t half = config->block_size / 2;
    if (half < 2 || half > 2048) half = 2048;
    size_t count = (size_t)s->channels * half;
    float* mags = (float*)calloc(count, sizeof(float));
    float* re = (float*)malloc(half * 2 * sizeof(float));
    float* im = (float*)malloc(half * 2 * sizeof(float));
    if (!mags || !re || !im) {
        free(mags); free(re); free(im);
        return ORPHEUS_ERR_OUT_OF_MEMORY;
    }
    free(s->algorithm.mags); free(s->algorithm.re); free(s->algorithm.im);
    s->algorithm.half = half;
    s->algorithm.magnitude_count = count;
    s->algorithm.mags = mags;
    s->algorithm.re = re;
    s->algorithm.im = im;
    memset(s->spectrum, 0, sizeof(s->spectrum));
    return ORPHEUS_OK;
}

int orpheus_builtin_psd_reset(orpheus_builtin_psd_State* s) {
    if (s->algorithm.mags)
        memset(s->algorithm.mags, 0, s->algorithm.magnitude_count * sizeof(float));
    memset(s->spectrum, 0, sizeof(s->spectrum));
    return ORPHEUS_OK;
}

int orpheus_builtin_psd_destroy(orpheus_builtin_psd_State* s) {
    free(s->algorithm.mags); free(s->algorithm.re); free(s->algorithm.im);
    memset(&s->algorithm, 0, sizeof(s->algorithm));
    return ORPHEUS_OK;
}

int orpheus_builtin_psd_algorithm(orpheus_builtin_psd_State* s,
    orpheus_builtin_psd_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!io->in.buffer || !io->out.buffer || io->in.buffer->frame_count < ctx->frame_count
        || !s->algorithm.mags
        || !s->algorithm.re || !s->algorithm.im) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t n = ctx->frame_count, half = s->algorithm.half;
    if (!(n & (n - 1)) && n / 2 == half) {
        float coefficient = 1.0f / s->smoothing;
        for (int32_t c = 0; c < s->channels; ++c) {
            for (uint32_t f = 0; f < n; ++f) {
                s->algorithm.re[f] = io->in.samples[(size_t)f * s->channels + c];
                s->algorithm.im[f] = 0.0f;
            }
            fft_radix2(s->algorithm.re, s->algorithm.im, n);
            float* mag = s->algorithm.mags + c * half;
            for (uint32_t k = 0; k < half; ++k) {
                float re = s->algorithm.re[k], im = s->algorithm.im[k];
                float magnitude = 2.0f * sqrtf(re * re + im * im) / (float)n;
                mag[k] += coefficient * (magnitude - mag[k]);
            }
        }
        for (uint32_t i = 0; i < 1023; ++i) {
            float source = (float)i * (float)(half - 1) / 1022.0f;
            uint32_t lower = (uint32_t)source, upper = lower + 1 < half ? lower + 1 : lower;
            float fraction = source - (float)lower;
            s->spectrum[i] = s->algorithm.mags[lower]
                + (s->algorithm.mags[upper] - s->algorithm.mags[lower]) * fraction;
        }
    }
    memmove(io->out.samples, io->in.samples, (size_t)n * s->channels * sizeof(float));
    return orpheus_builtin_psd_emit_out(ctx, &io->out);
}
