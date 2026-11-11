#include "component_state.h"
#include "component_api.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static void release(orpheus_builtin_probe_spectrum_AlgorithmState* a) {
    free(a->window); free(a->ring); free(a->re); free(a->im); free(a->mags);
    memset(a, 0, sizeof(*a));
}

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

int orpheus_builtin_probe_spectrum_prepare(orpheus_builtin_probe_spectrum_State* s,
    const OrpheusConfig* config) {
    (void)config;
    orpheus_builtin_probe_spectrum_AlgorithmState next = {0};
    next.size = 64;
    while (next.size < (uint32_t)s->window_size) next.size <<= 1;
    next.half = next.size / 2;
    next.window = (float*)malloc(next.size * sizeof(float));
    next.ring = (float*)calloc(next.size, sizeof(float));
    next.re = (float*)malloc(next.size * sizeof(float));
    next.im = (float*)malloc(next.size * sizeof(float));
    next.mags = (float*)calloc(next.half, sizeof(float));
    if (!next.window || !next.ring || !next.re || !next.im || !next.mags) {
        release(&next);
        return ORPHEUS_ERR_OUT_OF_MEMORY;
    }
    for (uint32_t i = 0; i < next.size; ++i)
        next.window[i] = 0.5f - 0.5f * cosf(2.0f * 3.14159265358979323846f * i / (next.size - 1));
    release(&s->algorithm);
    s->algorithm = next;
    s->window_size = (int32_t)next.size;
    memset(s->spectrum, 0, sizeof(s->spectrum));
    return ORPHEUS_OK;
}

int orpheus_builtin_probe_spectrum_reset(orpheus_builtin_probe_spectrum_State* s) {
    s->algorithm.ring_pos = s->algorithm.samples_seen = 0;
    if (s->algorithm.ring) memset(s->algorithm.ring, 0, s->algorithm.size * sizeof(float));
    if (s->algorithm.mags) memset(s->algorithm.mags, 0, s->algorithm.half * sizeof(float));
    memset(s->spectrum, 0, sizeof(s->spectrum));
    return ORPHEUS_OK;
}

int orpheus_builtin_probe_spectrum_destroy(orpheus_builtin_probe_spectrum_State* s) {
    release(&s->algorithm);
    return ORPHEUS_OK;
}

static void analyze(orpheus_builtin_probe_spectrum_State* s) {
    orpheus_builtin_probe_spectrum_AlgorithmState* a = &s->algorithm;
    for (uint32_t i = 0; i < a->size; ++i) {
        float x = a->ring[i] * a->window[i];
        a->re[i] = x;
        a->im[i] = 0.0f;
    }
    fft_radix2(a->re, a->im, a->size);
    for (uint32_t i = 0; i < a->half; ++i)
        a->mags[i] = 2.0f * sqrtf(a->re[i] * a->re[i] + a->im[i] * a->im[i]) / (float)a->size;
    for (uint32_t i = 0; i < 1023; ++i) {
        float source = (float)i * (float)(a->half - 1) / 1022.0f;
        uint32_t lower = (uint32_t)source, upper = lower + 1 < a->half ? lower + 1 : lower;
        float fraction = source - (float)lower;
        s->spectrum[i] = a->mags[lower] + (a->mags[upper] - a->mags[lower]) * fraction;
    }
}

int orpheus_builtin_probe_spectrum_algorithm(orpheus_builtin_probe_spectrum_State* s,
    orpheus_builtin_probe_spectrum_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    orpheus_builtin_probe_spectrum_AlgorithmState* a = &s->algorithm;
    if (!io->in.buffer || io->in.buffer->frame_count < ctx->frame_count
        || !a->window || !a->ring || !a->re || !a->im || !a->mags) return ORPHEUS_ERR_INVALID_ARG;
    for (uint32_t f = 0; f < ctx->frame_count; ++f) {
        a->ring[a->ring_pos] = io->in.samples[(size_t)f * s->channels];
        a->ring_pos = (a->ring_pos + 1) % a->size;
        if (++a->samples_seen >= a->size) {
            a->samples_seen = 0;
            analyze(s);
        }
    }
    if (io->out.buffer)
        memmove(io->out.samples, io->in.samples, (size_t)ctx->frame_count * s->channels * sizeof(float));
    return orpheus_builtin_probe_spectrum_emit_out(ctx, &io->out);
}
