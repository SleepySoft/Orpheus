#include "component_state.h"
#include "component_api.h"
#include <math.h>

int orpheus_builtin_switch_prepare(orpheus_builtin_switch_State* s, const OrpheusConfig* config) {
    s->algorithm.gain = s->enable >= 0.5f ? 1.0f : 0.0f;
    if (s->ramp_ms <= 0.0f || config->sample_rate == 0) s->algorithm.smoothing_coeff = 1.0f;
    else {
        s->algorithm.smoothing_coeff = 1.0f - expf(-1.0f /
            ((s->ramp_ms / 1000.0f) * (float)config->sample_rate));
        if (s->algorithm.smoothing_coeff > 1.0f) s->algorithm.smoothing_coeff = 1.0f;
    }
    return ORPHEUS_OK;
}

int orpheus_builtin_switch_reset(orpheus_builtin_switch_State* s) {
    s->enable = 1.0f;
    s->algorithm.gain = 1.0f;
    return ORPHEUS_OK;
}

int orpheus_builtin_switch_algorithm(orpheus_builtin_switch_State* s,
    orpheus_builtin_switch_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!io->in.buffer || !io->out.buffer || io->in.buffer->frame_count < ctx->frame_count)
        return ORPHEUS_ERR_INVALID_ARG;
    size_t count = (size_t)ctx->frame_count * s->channels, bytes = count * sizeof(float);
    uintptr_t x = (uintptr_t)io->in.samples, y = (uintptr_t)io->out.samples;
    if (x != y && (x >= y ? x - y < bytes : y - x < bytes)) return ORPHEUS_ERR_INVALID_ARG;
    float target = s->enable >= 0.5f ? 1.0f : 0.0f;
    for (size_t i = 0; i < count; ++i) {
        s->algorithm.gain += s->algorithm.smoothing_coeff * (target - s->algorithm.gain);
        io->out.samples[i] = io->in.samples[i] * s->algorithm.gain;
    }
    return orpheus_builtin_switch_emit_out(ctx, &io->out);
}
