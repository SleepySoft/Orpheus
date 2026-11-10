#include "component_state.h"
#include "component_api.h"
#include <math.h>
#include <string.h>

int orpheus_builtin_probe_rms_prepare(orpheus_builtin_probe_rms_State* state,
    const OrpheusConfig* config) {
    (void)config;
    state->rms = 0.0f;
    return ORPHEUS_OK;
}

int orpheus_builtin_probe_rms_reset(orpheus_builtin_probe_rms_State* state) {
    state->rms = 0.0f;
    return ORPHEUS_OK;
}

int orpheus_builtin_probe_rms_algorithm(orpheus_builtin_probe_rms_State* state,
    orpheus_builtin_probe_rms_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!io->in.buffer || !io->out.buffer) return ORPHEUS_ERR_INVALID_ARG;
    size_t count = (size_t)ctx->frame_count * state->channels;
    double sum = 0.0;
    for (size_t i = 0; i < count; ++i) {
        double sample = io->in.samples[i];
        sum += sample * sample;
    }
    memmove(io->out.samples, io->in.samples, count * sizeof(float));
    state->rms = (float)sqrt(sum / count);
    return orpheus_builtin_probe_rms_emit_out(ctx, &io->out);
}
