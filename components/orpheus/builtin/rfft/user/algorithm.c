#include "component_state.h"
#include "component_api.h"
#include <math.h>
#include <string.h>

#define PI_F 3.14159265358979f

int orpheus_builtin_rfft_normalize(orpheus_builtin_rfft_Parameters* p, const char* key) {
    (void)key;
    return p->fft_size >= 4 && !(p->fft_size & (p->fft_size - 1)) &&
        (!strcmp(p->output_mode, "halfcomplex") || !strcmp(p->output_mode, "magnitude") ||
         !strcmp(p->output_mode, "power"))
        ? ORPHEUS_OK : ORPHEUS_ERR_INVALID_ARG;
}

int orpheus_builtin_rfft_prepare(orpheus_builtin_rfft_State* s, const OrpheusConfig* config) {
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

int orpheus_builtin_rfft_algorithm(orpheus_builtin_rfft_State* s,
    orpheus_builtin_rfft_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (ctx->trigger_input != io->in.input_index || !io->in.buffer) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t n = (uint32_t)s->fft_size, channels = (uint32_t)s->channels;
    uint32_t frames = n * (uint32_t)s->batches;
    size_t bytes = (size_t)frames * channels * sizeof(float);
    if (overlap(io->in.samples, bytes, s, sizeof(*s))) return ORPHEUS_ERR_INVALID_ARG;
    float* outputs[] = {io->out.samples, io->spectrum.samples};
    for (unsigned i = 0; i < 2; ++i) {
        if (outputs[i] && (overlap(outputs[i], bytes, io->in.samples, bytes) ||
            overlap(outputs[i], bytes, s, sizeof(*s)))) return ORPHEUS_ERR_INVALID_ARG;
    }
    if (outputs[0] && outputs[1] && overlap(outputs[0], bytes, outputs[1], bytes))
        return ORPHEUS_ERR_INVALID_ARG;
    unsigned mode = !strcmp(s->output_mode, "power") ? 2 : !strcmp(s->output_mode, "magnitude");
    for (uint32_t frame = 0; frame < frames; frame += n) {
        for (uint32_t ch = 0; ch < channels; ++ch) {
            float* re = s->algorithm.scratchR;
            float* im = s->algorithm.scratchI;
            for (uint32_t k = 0; k < n; ++k) {
                re[k] = io->in.samples[(size_t)(frame + k) * channels + ch];
                im[k] = 0.0f;
            }
            fft_forward(re, im, n, s->algorithm.twiddleCos, s->algorithm.twiddleSin);
            for (unsigned slot = 0; slot < 2; ++slot) {
                if (!outputs[slot]) continue;
                float* dst = outputs[slot] + (size_t)frame * channels + ch;
                if (slot == 1 || mode == 0) {
                    dst[0] = re[0];
                    for (uint32_t k = 1; k < n / 2; ++k) dst[k * channels] = re[k];
                    dst[n / 2 * channels] = re[n / 2];
                    for (uint32_t k = 1; k < n / 2; ++k) dst[(n - k) * channels] = im[k];
                } else {
                    for (uint32_t k = 0; k < n; ++k) {
                        uint32_t idx = k <= n / 2 ? k : n - k;
                        float r = re[idx], imaginary = idx == 0 || idx == n / 2 ? 0.0f : im[idx];
                        float value = r * r + imaginary * imaginary;
                        dst[k * channels] = mode == 1 ? sqrtf(value) : value;
                    }
                }
            }
        }
    }
    int rc = orpheus_builtin_rfft_emit_out(ctx, &io->out);
    return rc ? rc : orpheus_builtin_rfft_emit_spectrum(ctx, &io->spectrum);
}
