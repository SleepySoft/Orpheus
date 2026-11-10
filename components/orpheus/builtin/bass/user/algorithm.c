#include "component_state.h"
#include <math.h>
#include <string.h>

int orpheus_builtin_bass_prepare(orpheus_builtin_bass_State* s, const OrpheusConfig* config) {
    s->algorithm.a1 = config->sample_rate > 0
        ? expf(-2.0f * 3.14159265358979f * s->fc / (float)config->sample_rate) : 0.0f;
    s->algorithm.target_boost = powf(10.0f, s->gain_db / 20.0f) - 1.0f;
    s->algorithm.boost = s->algorithm.target_boost;
    memset(s->algorithm.z, 0, sizeof(s->algorithm.z));
    s->algorithm.smoothing_coeff = 1.0f;
    if (s->ramp_ms > 0.0f && config->sample_rate > 0) {
        s->algorithm.smoothing_coeff = 1.0f - expf(-1.0f / ((s->ramp_ms / 1000.0f) * (float)config->sample_rate));
        if (s->algorithm.smoothing_coeff > 1.0f) s->algorithm.smoothing_coeff = 1.0f;
    }
    return ORPHEUS_OK;
}

int orpheus_builtin_bass_reset(orpheus_builtin_bass_State* s) {
    memset(s->algorithm.z, 0, sizeof(s->algorithm.z));
    return ORPHEUS_OK;
}

int orpheus_builtin_bass_set_parameter(orpheus_builtin_bass_State* s, const char* key, const OrpheusValue* value) {
    if (strcmp(key, "gain_db") != 0) return ORPHEUS_ERR_NOT_FOUND;
    s->algorithm.target_boost = powf(10.0f, value->value.f32 / 20.0f) - 1.0f;
    return ORPHEUS_OK;
}

int orpheus_builtin_bass_get_parameter(orpheus_builtin_bass_State* s, const char* key, OrpheusValue* value) {
    if (strcmp(key, "gain_db") != 0) return ORPHEUS_ERR_NOT_FOUND;
    value->type = ORPHEUS_VALUE_FLOAT;
    value->value.f32 = 20.0f * log10f(s->algorithm.boost + 1.0f);
    return ORPHEUS_OK;
}

int orpheus_builtin_bass_algorithm(orpheus_builtin_bass_State* s, orpheus_builtin_bass_IO* io, const OrpheusProcessContext* ctx) {
    if (!io->in.buffer || !io->out.buffer) return ORPHEUS_ERR_INVALID_ARG;
    float b0 = 1.0f - s->algorithm.a1;
    for (uint32_t n = 0; n < ctx->frame_count; ++n) {
        s->algorithm.boost += s->algorithm.smoothing_coeff * (s->algorithm.target_boost - s->algorithm.boost);
        for (uint32_t c = 0; c < (uint32_t)s->channels; ++c) {
            float x = io->in.samples[n * s->channels + c];
            float lpf = b0 * x + s->algorithm.a1 * s->algorithm.z[c];
            s->algorithm.z[c] = lpf;
            io->out.samples[n * s->channels + c] = x + s->algorithm.boost * lpf;
        }
    }
    io->out.buffer->frame_count = ctx->frame_count;
    return orpheus_builtin_bass_emit_out(ctx, &io->out);
}
