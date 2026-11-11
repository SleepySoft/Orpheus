#include "component_state.h"
#include "component_api.h"
#include <math.h>

int orpheus_builtin_spatial_enhancer_reset(orpheus_builtin_spatial_enhancer_State* s) {
    s->algorithm.width_smoothed = s->width;
    s->algorithm.air_smoothed = s->air;
    s->algorithm.mono_mix_smoothed = s->mono_mix;
    s->algorithm.hp_prev_side = 0.0f;
    s->algorithm.hp_prev_out = 0.0f;
    return ORPHEUS_OK;
}

int orpheus_builtin_spatial_enhancer_prepare(orpheus_builtin_spatial_enhancer_State* s,
    const OrpheusConfig* config) {
    s->algorithm.mono_coeff = 1.0f;
    if (config->sample_rate > 0) {
        float tau = 0.005f;
        s->algorithm.mono_coeff = 1.0f - expf(-1.0f / (tau * (float)config->sample_rate));
        if (s->algorithm.mono_coeff > 1.0f) s->algorithm.mono_coeff = 1.0f;
        float fc = s->air_fc < 500.0f ? 500.0f : s->air_fc;
        float w = 2.0f * 3.14159265f * fc / (float)config->sample_rate;
        float cw = cosf(w), sw = sinf(w);
        float alpha = (1.0f - cw) / (1.0f + sw);
        s->algorithm.hp_fb = alpha;
        s->algorithm.hp_gain = (1.0f + alpha) * 0.5f;
    }
    return orpheus_builtin_spatial_enhancer_reset(s);
}

int orpheus_builtin_spatial_enhancer_algorithm(orpheus_builtin_spatial_enhancer_State* s,
    orpheus_builtin_spatial_enhancer_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!io->in.buffer || !io->out.buffer || io->in.buffer->frame_count < ctx->frame_count)
        return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes = (size_t)ctx->frame_count * 2 * sizeof(float);
    uintptr_t x = (uintptr_t)io->in.samples, y = (uintptr_t)io->out.samples;
    if (x >= y ? x - y < bytes : y - x < bytes) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_spatial_enhancer_AlgorithmState* a = &s->algorithm;
    for (uint32_t n = 0; n < ctx->frame_count; ++n) {
        float left = io->in.samples[n * 2], right = io->in.samples[n * 2 + 1];
        float mid = (left + right) * 0.5f;
        float side = (left - right) * 0.5f;
        a->width_smoothed += a->mono_coeff * (s->width - a->width_smoothed);
        a->air_smoothed += a->mono_coeff * (s->air - a->air_smoothed);
        a->mono_mix_smoothed += a->mono_coeff * (s->mono_mix - a->mono_mix_smoothed);
        float hp = a->hp_gain * (side - a->hp_prev_side) + a->hp_fb * a->hp_prev_out;
        a->hp_prev_side = side;
        a->hp_prev_out = hp;
        float side_hp = side * (1.0f - a->air_smoothed) + hp * a->air_smoothed;
        float side_eff = side_hp * a->width_smoothed;
        float side_out = side_eff * a->mono_mix_smoothed;
        io->out.samples[n * 2] = mid + side_out;
        io->out.samples[n * 2 + 1] = mid - side_out;
    }
    return orpheus_builtin_spatial_enhancer_emit_out(ctx, &io->out);
}
