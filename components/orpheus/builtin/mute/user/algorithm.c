#include "component_state.h"
#include <math.h>
#include <string.h>

int orpheus_builtin_mute_prepare(orpheus_builtin_mute_State* s, const OrpheusConfig* config) {
    s->algorithm.target_linear = s->mute > 0.5f ? 0.0f : 1.0f;
    s->algorithm.gain_linear = s->algorithm.target_linear;
    s->algorithm.smoothing_coeff = 1.0f;
    if (s->ramp_ms > 0.0f && config->sample_rate != 0) {
        float tau = s->ramp_ms / 1000.0f;
        s->algorithm.smoothing_coeff = 1.0f - expf(-1.0f / (tau * (float)config->sample_rate));
        if (s->algorithm.smoothing_coeff > 1.0f) s->algorithm.smoothing_coeff = 1.0f;
    }
    return ORPHEUS_OK;
}

int orpheus_builtin_mute_reset(orpheus_builtin_mute_State* s) {
    s->algorithm.target_linear = s->mute > 0.5f ? 0.0f : 1.0f;
    s->algorithm.gain_linear = s->algorithm.target_linear;
    return ORPHEUS_OK;
}

int orpheus_builtin_mute_set_parameter(orpheus_builtin_mute_State* s,
        const char* key, const OrpheusValue* value) {
    if (strcmp(key, "mute") != 0) return ORPHEUS_ERR_NOT_FOUND;
    float mute = value->value.f32;
    s->mute = mute;
    s->algorithm.target_linear = mute > 0.5f ? 0.0f : 1.0f;
    return ORPHEUS_OK;
}

int orpheus_builtin_mute_algorithm(orpheus_builtin_mute_State* s,
        orpheus_builtin_mute_IO* io, const OrpheusProcessContext* ctx) {
    if (!io->in.buffer || !io->out.buffer) return ORPHEUS_ERR_INVALID_ARG;
    /* 保留原核按交错样本推进包络的顺序，不改成逐帧斜坡。 */
    for (uint32_t i = 0; i < ctx->frame_count * (uint32_t)s->channels; ++i) {
        s->algorithm.gain_linear += s->algorithm.smoothing_coeff * (s->algorithm.target_linear - s->algorithm.gain_linear);
        io->out.samples[i] = io->in.samples[i] * s->algorithm.gain_linear;
    }
    io->out.buffer->frame_count = ctx->frame_count;
    return orpheus_builtin_mute_emit_out(ctx, &io->out);
}
