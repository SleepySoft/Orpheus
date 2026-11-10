#include "component_state.h"
#include "component_api.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

int orpheus_builtin_bulk_guarded_slew_prepare(orpheus_builtin_bulk_guarded_slew_State* state,
    const OrpheusConfig* config) {
    (void)config;
    size_t count = (size_t)state->channels * state->elements;
    if (count > SIZE_MAX / sizeof(*state->algorithm.history)) return ORPHEUS_ERR_OUT_OF_MEMORY;
    orpheus_builtin_bulk_guarded_slew_History* history =
        (orpheus_builtin_bulk_guarded_slew_History*)calloc(count, sizeof(*history));
    if (!history) return ORPHEUS_ERR_OUT_OF_MEMORY;
    free(state->algorithm.history);
    state->algorithm.history = history;
    state->algorithm.count = count;
    state->algorithm.initialized = false;
    return ORPHEUS_OK;
}

int orpheus_builtin_bulk_guarded_slew_reset(orpheus_builtin_bulk_guarded_slew_State* state) {
    if (state->algorithm.history)
        memset(state->algorithm.history, 0, state->algorithm.count * sizeof(*state->algorithm.history));
    state->algorithm.initialized = false;
    return ORPHEUS_OK;
}

int orpheus_builtin_bulk_guarded_slew_destroy(orpheus_builtin_bulk_guarded_slew_State* state) {
    free(state->algorithm.history);
    state->algorithm.history = NULL;
    state->algorithm.count = 0;
    state->algorithm.initialized = false;
    return ORPHEUS_OK;
}

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x >= y ? x - y < nb : y - x < na;
}

int orpheus_builtin_bulk_guarded_slew_algorithm(orpheus_builtin_bulk_guarded_slew_State* state,
    orpheus_builtin_bulk_guarded_slew_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!state->algorithm.history || !io->in.buffer) return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes = state->algorithm.count * sizeof(float);
    if (io->out.buffer && (overlap(io->in.samples, bytes, io->out.samples, bytes)
        || (io->bound.buffer && overlap(io->bound.samples, sizeof(float), io->out.samples, bytes))))
        return ORPHEUS_ERR_INVALID_ARG;
    float flag = io->bound.buffer ? io->bound.samples[0] : 0.0f;
    for (size_t i = 0; i < state->algorithm.count; ++i) {
        orpheus_builtin_bulk_guarded_slew_History* history = state->algorithm.history + i;
        float previous = state->algorithm.initialized ? history->value : state->initial;
        float delta = io->in.samples[i] - previous;
        float change = delta > 0 ? fminf(state->attack, delta) : fmaxf(state->decay, delta);
        volatile float product = history->delta * delta;
        float slew = product > 0 ? history->change : 0.0f;
        if (flag != 0 && slew == 0)
            slew = delta > 0 ? fminf(state->slow, delta) : fmaxf(-state->slow, delta);
        if (fabsf(delta) <= state->threshold && fabsf(slew) < fabsf(change)) change = slew;
        float next = previous + change;
        if (io->out.buffer) memcpy(io->out.samples + i, &next, sizeof(float));
        history->value = next;
        history->delta = delta;
        history->change = change;
    }
    state->algorithm.initialized = true;
    return io->out.buffer ? orpheus_builtin_bulk_guarded_slew_emit_out(ctx, &io->out) : ORPHEUS_OK;
}
