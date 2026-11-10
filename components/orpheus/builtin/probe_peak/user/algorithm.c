#include "component_state.h"
#include "component_api.h"
#include <math.h>
#include <string.h>

int orpheus_builtin_probe_peak_prepare(orpheus_builtin_probe_peak_State* state,
    const OrpheusConfig* config) {
    (void)config;
    state->peak = 0.0f;
    return ORPHEUS_OK;
}

int orpheus_builtin_probe_peak_reset(orpheus_builtin_probe_peak_State* state) {
    state->peak = 0.0f;
    return ORPHEUS_OK;
}

int orpheus_builtin_probe_peak_algorithm(orpheus_builtin_probe_peak_State* state,
    orpheus_builtin_probe_peak_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!io->in.buffer || !io->out.buffer) return ORPHEUS_ERR_INVALID_ARG;
    size_t count = (size_t)ctx->frame_count * state->channels;
    float peak = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        float sample = fabsf(io->in.samples[i]);
        if (sample > peak) peak = sample;
    }
    memmove(io->out.samples, io->in.samples, count * sizeof(float));
    state->peak = peak;
    return orpheus_builtin_probe_peak_emit_out(ctx, &io->out);
}
