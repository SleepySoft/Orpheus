#include "component_state.h"
#include "component_api.h"
#include <stdlib.h>
#include <string.h>

int orpheus_builtin_dual_trapezoid_smooth_prepare(orpheus_builtin_dual_trapezoid_smooth_State* s,
    const OrpheusConfig* config) {
    (void)config;
    size_t count = (size_t)s->channels * s->bins;
    if (count > SIZE_MAX / (3 * sizeof(float))) return ORPHEUS_ERR_OUT_OF_MEMORY;
    float* history = (float*)calloc(count * 3, sizeof(float));
    if (!history) return ORPHEUS_ERR_OUT_OF_MEMORY;
    free(s->algorithm.history);
    s->algorithm.history = history;
    s->algorithm.count = count;
    s->algorithm.initialized = false;
    return ORPHEUS_OK;
}

int orpheus_builtin_dual_trapezoid_smooth_reset(orpheus_builtin_dual_trapezoid_smooth_State* s) {
    if (s->algorithm.history)
        memset(s->algorithm.history, 0, s->algorithm.count * 3 * sizeof(float));
    s->algorithm.initialized = false;
    return ORPHEUS_OK;
}

int orpheus_builtin_dual_trapezoid_smooth_destroy(orpheus_builtin_dual_trapezoid_smooth_State* s) {
    free(s->algorithm.history);
    s->algorithm.history = NULL;
    s->algorithm.count = 0;
    s->algorithm.initialized = false;
    return ORPHEUS_OK;
}

static bool overlap(const void* a, const void* b, size_t bytes) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x >= y ? x - y < bytes : y - x < bytes;
}

int orpheus_builtin_dual_trapezoid_smooth_algorithm(orpheus_builtin_dual_trapezoid_smooth_State* s,
    orpheus_builtin_dual_trapezoid_smooth_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!s->algorithm.history || !io->in.buffer) return ORPHEUS_ERR_INVALID_ARG;
    size_t points = s->algorithm.count, bytes = points * s->batches * sizeof(float);
    if ((io->fast.buffer && overlap(io->in.samples, io->fast.samples, bytes))
        || (io->slow.buffer && overlap(io->in.samples, io->slow.samples, bytes))
        || (io->fast.buffer && io->slow.buffer && overlap(io->fast.samples, io->slow.samples, bytes)))
        return ORPHEUS_ERR_INVALID_ARG;
    if (!s->algorithm.initialized) {
        for (size_t i = 0; i < points * 3; ++i) s->algorithm.history[i] = s->initial;
        s->algorithm.initialized = true;
    }
    float* previous = s->algorithm.history;
    float* histories[] = {previous + points, previous + points * 2};
    float* outputs[] = {io->fast.samples, io->slow.samples};
    const float alphas[] = {s->fast_alpha, s->slow_alpha};
    const float factors[] = {(1.0f - alphas[0]) * .5f, (1.0f - alphas[1]) * .5f};
    for (int32_t h = 0; h < s->batches; ++h) for (size_t i = 0; i < points; ++i) {
        size_t index = h * points + i;
        float current = io->in.samples[index], pair = current + previous[i];
        for (unsigned p = 0; p < 2; ++p) {
            volatile float feed = pair * factors[p], feedback = histories[p][i] * alphas[p];
            float value = feed + feedback;
            histories[p][i] = value;
            if (outputs[p]) outputs[p][index] = value;
        }
        previous[i] = current;
    }
    int rc = orpheus_builtin_dual_trapezoid_smooth_emit_fast(ctx, &io->fast);
    return rc ? rc : orpheus_builtin_dual_trapezoid_smooth_emit_slow(ctx, &io->slow);
}
