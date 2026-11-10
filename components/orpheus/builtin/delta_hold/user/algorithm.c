#include "component_state.h"
#include "component_api.h"
#include <string.h>

int orpheus_builtin_delta_hold_reset(orpheus_builtin_delta_hold_State* s) {
    memset(&s->algorithm, 0, sizeof(s->algorithm));
    return ORPHEUS_OK;
}

int orpheus_builtin_delta_hold_prepare(orpheus_builtin_delta_hold_State* s, const OrpheusConfig* config) {
    (void)config;
    return orpheus_builtin_delta_hold_reset(s);
}

int orpheus_builtin_delta_hold_algorithm(orpheus_builtin_delta_hold_State* s,
    orpheus_builtin_delta_hold_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!io->in.buffer) return ORPHEUS_ERR_INVALID_ARG;
    if (io->out.buffer) {
        size_t bytes = (size_t)s->channels * s->batches * sizeof(float);
        uintptr_t input = (uintptr_t)io->in.samples, output = (uintptr_t)io->out.samples;
        if (input >= output ? input - output < bytes : output - input < bytes)
            return ORPHEUS_ERR_INVALID_ARG;
    }
    for (int32_t h = 0; h < s->batches; ++h) for (int32_t c = 0; c < s->channels; ++c) {
        size_t i = (size_t)h * s->channels + c;
        float current = io->in.samples[i];
        volatile float delta = current - s->algorithm.previous[c];
        int32_t active;
        if (delta > s->threshold) {
            active = 1;
            s->algorithm.counter[c] = 0;
        } else if (s->algorithm.active[c] == 1 && s->algorithm.counter[c] < (uint32_t)s->hold_ticks) {
            active = 1;
            ++s->algorithm.counter[c];
        } else active = 0;
        s->algorithm.active[c] = active;
        s->algorithm.previous[c] = current;
        if (io->out.buffer) io->out.samples[i] = active;
    }
    return orpheus_builtin_delta_hold_emit_out(ctx, &io->out);
}
