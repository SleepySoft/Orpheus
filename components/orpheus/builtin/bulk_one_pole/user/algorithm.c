#include "component_state.h"
#include "component_api.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

int orpheus_builtin_bulk_one_pole_prepare(orpheus_builtin_bulk_one_pole_State* state,
    const OrpheusConfig* config) {
    (void)config;
    size_t count = (size_t)state->channels * state->elements;
    if (count > SIZE_MAX / sizeof(float)) return ORPHEUS_ERR_OUT_OF_MEMORY;
    float* history = (float*)calloc(count, sizeof(float));
    if (!history) return ORPHEUS_ERR_OUT_OF_MEMORY;
    free(state->algorithm.history);
    state->algorithm.history = history;
    state->algorithm.count = count;
    return ORPHEUS_OK;
}

int orpheus_builtin_bulk_one_pole_reset(orpheus_builtin_bulk_one_pole_State* state) {
    if (state->algorithm.history)
        memset(state->algorithm.history, 0, state->algorithm.count * sizeof(float));
    return ORPHEUS_OK;
}

int orpheus_builtin_bulk_one_pole_destroy(orpheus_builtin_bulk_one_pole_State* state) {
    free(state->algorithm.history);
    state->algorithm.history = NULL;
    state->algorithm.count = 0;
    return ORPHEUS_OK;
}

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x >= y ? x - y < nb : y - x < na;
}

int orpheus_builtin_bulk_one_pole_algorithm(orpheus_builtin_bulk_one_pole_State* state,
    orpheus_builtin_bulk_one_pole_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!state->algorithm.history || !io->in.buffer) return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes = state->algorithm.count * sizeof(float);
    if (io->out.buffer && (overlap(io->in.samples, bytes, io->out.samples, bytes)
        || (io->update.buffer && overlap(io->update.samples, sizeof(int32_t), io->out.samples, bytes))))
        return ORPHEUS_ERR_INVALID_ARG;
    if (!io->update.buffer || io->update.samples[0]) {
        float factor = 1.0f - state->alpha;
        for (size_t i = 0; i < state->algorithm.count; ++i) {
            float value = state->absolute_input ? fabsf(io->in.samples[i]) : io->in.samples[i];
            volatile float feed = factor * value, feedback = state->algorithm.history[i] * state->alpha;
            state->algorithm.history[i] = feed + feedback;
        }
    }
    if (!io->out.buffer) return ORPHEUS_OK;
    memcpy(io->out.samples, state->algorithm.history, bytes);
    return orpheus_builtin_bulk_one_pole_emit_out(ctx, &io->out);
}
