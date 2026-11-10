#include "component_state.h"
#include "component_api.h"
#include <stdlib.h>
#include <string.h>

int orpheus_builtin_trapezoid_ratio_prepare(orpheus_builtin_trapezoid_ratio_State* s,
    const OrpheusConfig* config) {
    (void)config;
    size_t count = (size_t)s->channels * s->bins;
    if (count > SIZE_MAX / (4 * sizeof(float))) return ORPHEUS_ERR_OUT_OF_MEMORY;
    float* history = (float*)calloc(count * 4, sizeof(float));
    if (!history) return ORPHEUS_ERR_OUT_OF_MEMORY;
    free(s->algorithm.history);
    s->algorithm.history = history;
    s->algorithm.count = count;
    return ORPHEUS_OK;
}

int orpheus_builtin_trapezoid_ratio_reset(orpheus_builtin_trapezoid_ratio_State* s) {
    if (s->algorithm.history)
        memset(s->algorithm.history, 0, s->algorithm.count * 4 * sizeof(float));
    return ORPHEUS_OK;
}

int orpheus_builtin_trapezoid_ratio_destroy(orpheus_builtin_trapezoid_ratio_State* s) {
    free(s->algorithm.history);
    s->algorithm.history = NULL;
    s->algorithm.count = 0;
    return ORPHEUS_OK;
}

static bool overlap(const void* a, const void* b, size_t bytes) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x >= y ? x - y < bytes : y - x < bytes;
}

int orpheus_builtin_trapezoid_ratio_algorithm(orpheus_builtin_trapezoid_ratio_State* s,
    orpheus_builtin_trapezoid_ratio_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!s->algorithm.history || !io->numerator.buffer || !io->denominator.buffer)
        return ORPHEUS_ERR_INVALID_ARG;
    size_t points = s->algorithm.count, bytes = points * s->batches * sizeof(float);
    if (io->out.buffer && (overlap(io->numerator.samples, io->out.samples, bytes)
        || overlap(io->denominator.samples, io->out.samples, bytes)))
        return ORPHEUS_ERR_INVALID_ARG;
    const float* inputs[] = {io->numerator.samples, io->denominator.samples};
    const float factor = (1.0f - s->alpha) * .5f;
    for (int32_t h = 0; h < s->batches; ++h) for (size_t i = 0; i < points; ++i) {
        size_t index = h * points + i;
        float smoothed[2];
        for (unsigned p = 0; p < 2; ++p) {
            float* previous = s->algorithm.history + p * points * 2;
            float* last = previous + points;
            float current = inputs[p][index], pair = current + previous[i];
            volatile float feed = pair * factor, feedback = last[i] * s->alpha;
            smoothed[p] = feed + feedback;
            previous[i] = current;
            last[i] = smoothed[p];
        }
        volatile float denominator = smoothed[1] + s->epsilon;
        if (io->out.buffer) {
            if (s->division_mode) {
                volatile float reciprocal = 1.0f / denominator;
                io->out.samples[index] = reciprocal * smoothed[0];
            } else io->out.samples[index] = smoothed[0] / denominator;
        }
    }
    return orpheus_builtin_trapezoid_ratio_emit_out(ctx, &io->out);
}
