#include "component_state.h"
#include "component_api.h"
#include <string.h>

static float per_sample(float rate, double sample_rate) {
    return sample_rate > 0 ? rate / (float)sample_rate : 0.0001f;
}

int orpheus_builtin_noise_slew_prepare(orpheus_builtin_noise_slew_State* s, const OrpheusConfig* config) {
    s->algorithm.sample_rate = config->sample_rate;
    s->algorithm.rise_delta = per_sample(s->rise_rate, config->sample_rate);
    s->algorithm.fall_delta = per_sample(s->fall_rate, config->sample_rate);
    memset(s->algorithm.prev, 0, sizeof(s->algorithm.prev));
    return ORPHEUS_OK;
}

int orpheus_builtin_noise_slew_reset(orpheus_builtin_noise_slew_State* s) {
    memset(s->algorithm.prev, 0, sizeof(s->algorithm.prev));
    return ORPHEUS_OK;
}

int orpheus_builtin_noise_slew_set_parameter(orpheus_builtin_noise_slew_State* s,
    const char* id, const OrpheusValue* value) {
    if (!strcmp(id, "rise_rate"))
        s->algorithm.rise_delta = per_sample(value->value.f32, s->algorithm.sample_rate);
    else if (!strcmp(id, "fall_rate"))
        s->algorithm.fall_delta = per_sample(value->value.f32, s->algorithm.sample_rate);
    else return ORPHEUS_ERR_NOT_FOUND;
    return ORPHEUS_OK;
}

int orpheus_builtin_noise_slew_algorithm(orpheus_builtin_noise_slew_State* s,
    orpheus_builtin_noise_slew_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!io->in.buffer || !io->out.buffer || io->in.buffer->frame_count < ctx->frame_count)
        return ORPHEUS_ERR_INVALID_ARG;
    size_t count = (size_t)ctx->frame_count * s->channels;
    size_t bytes = count * sizeof(float);
    uintptr_t x = (uintptr_t)io->in.samples, y = (uintptr_t)io->out.samples;
    if (x != y && (x >= y ? x - y < bytes : y - x < bytes)) return ORPHEUS_ERR_INVALID_ARG;
    for (size_t i = 0; i < count; ++i) {
        uint32_t c = (uint32_t)(i % s->channels);
        float delta = io->in.samples[i] - s->algorithm.prev[c];
        float maxd = delta > 0.0f ? s->algorithm.rise_delta : s->algorithm.fall_delta;
        if (delta > maxd) delta = maxd;
        else if (delta < -maxd) delta = -maxd;
        float next = s->algorithm.prev[c] + delta;
        s->algorithm.prev[c] = next;
        io->out.samples[i] = next;
    }
    return orpheus_builtin_noise_slew_emit_out(ctx, &io->out);
}
