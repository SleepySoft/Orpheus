#include "component_state.h"
#include "component_api.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static float db_to_linear(float db) {
    return powf(10.0f, db / 20.0f);
}

int orpheus_builtin_gain_prepare(orpheus_builtin_gain_State* state,
    const OrpheusConfig* config) {
    int configured_channels = 0;
    for (uint32_t i = 0; i < config->param_count; ++i)
        if (config->param_ids[i] && !strcmp(config->param_ids[i], "channels"))
            configured_channels = 1;
    if (!configured_channels && config->channels)
        state->channels = (int32_t)config->channels;
    if (state->channels < 1 || state->channels > 32 ||
        state->linear_a1 <= -1.0f || state->linear_a1 >= 1.0f ||
        (state->gains[0] && state->gain_mode != 2))
        return ORPHEUS_ERR_INVALID_ARG;
    const char* cursor = state->gains;
    for (int32_t c = 0; c < state->channels; ++c) {
        float gain = 1.0f;
        if (state->gains[0]) {
            char* end;
            gain = strtof(cursor, &end);
            if (end == cursor || !isfinite(gain)) return ORPHEUS_ERR_INVALID_ARG;
            while (isspace((unsigned char)*end)) ++end;
            if (c + 1 < state->channels ? *end != ',' : *end != '\0')
                return ORPHEUS_ERR_INVALID_ARG;
            cursor = c + 1 < state->channels ? end + 1 : end;
        }
        state->algorithm.per_channel_gains[c] = gain;
    }
    state->algorithm.linear_state = 0.0f;
    state->algorithm.gain_linear = state->gain_mode == 1 ? 0.0f : db_to_linear(state->gain_db);
    state->algorithm.target_linear = state->gain_mode == 1
        ? state->gain_linear : state->algorithm.gain_linear;
    state->algorithm.smoothing_coeff = 1.0f;
    if (state->smoothing_ms > 0.0f && config->sample_rate) {
        const float tau = state->smoothing_ms / 1000.0f;
        state->algorithm.smoothing_coeff = 1.0f - expf(-1.0f / (tau * (float)config->sample_rate));
        if (state->algorithm.smoothing_coeff > 1.0f)
            state->algorithm.smoothing_coeff = 1.0f;
    }
    return ORPHEUS_OK;
}

int orpheus_builtin_gain_reset(orpheus_builtin_gain_State* state) {
    if (state->gain_mode == 2) return ORPHEUS_OK;
    if (state->gain_mode == 1) {
        state->algorithm.linear_state = 0.0f;
        state->algorithm.gain_linear = 0.0f;
    } else {
        state->gain_db = 0.0f;
        state->algorithm.gain_linear = db_to_linear(0.0f);
        state->algorithm.target_linear = state->algorithm.gain_linear;
    }
    return ORPHEUS_OK;
}

int orpheus_builtin_gain_set_parameter(orpheus_builtin_gain_State* state,
    const char* id, const OrpheusValue* value) {
    if (!strcmp(id, "gain_linear")) {
        if (state->gain_mode != 1) return ORPHEUS_ERR_UNSUPPORTED;
        state->algorithm.target_linear = value->value.f32;
        return ORPHEUS_OK;
    }
    if (!strcmp(id, "gain_db")) {
        if (state->gain_mode != 0) return ORPHEUS_ERR_UNSUPPORTED;
        state->algorithm.target_linear = db_to_linear(value->value.f32);
        return ORPHEUS_OK;
    }
    return ORPHEUS_ERR_NOT_FOUND;
}

int orpheus_builtin_gain_register_slots(orpheus_builtin_gain_State* state,
    const OrpheusRegistry* registry) {
    return state->gain_mode == 2 ? orpheus_builtin_gain_register_gains(state, registry) : ORPHEUS_OK;
}

int orpheus_builtin_gain_algorithm(orpheus_builtin_gain_State* state,
    orpheus_builtin_gain_IO* io, const OrpheusProcessContext* ctx) {
    if (!io->in.buffer || !io->out.buffer ||
        io->in.buffer->channels != (uint32_t)state->channels ||
        io->out.buffer->channels != (uint32_t)state->channels ||
        io->in.buffer->frame_count < ctx->frame_count)
        return ORPHEUS_ERR_INVALID_ARG;
    const uint32_t channels = (uint32_t)state->channels;
    if (state->gain_mode == 2) {
        for (uint32_t n = 0; n < ctx->frame_count; ++n)
            for (uint32_t c = 0; c < channels; ++c)
                io->out.samples[n * channels + c] =
                    io->in.samples[n * channels + c] * state->algorithm.per_channel_gains[c];
    } else if (state->gain_mode == 1) {
        for (uint32_t n = 0; n < ctx->frame_count; ++n) {
            float gain = state->gain_linear;
            if (state->linear_b0 != 1.0f || state->linear_a1 != 0.0f) {
                gain = state->linear_b0 * state->gain_linear + state->algorithm.linear_state;
                state->algorithm.linear_state = 0.0f - state->linear_a1 * gain;
            }
            state->algorithm.gain_linear = gain;
            for (uint32_t c = 0; c < channels; ++c)
                io->out.samples[n * channels + c] = io->in.samples[n * channels + c] * gain;
        }
    } else {
        const float target = db_to_linear(state->gain_db);
        state->algorithm.target_linear = target;
        for (size_t i = 0; i < (size_t)ctx->frame_count * channels; ++i) {
            state->algorithm.gain_linear += state->algorithm.smoothing_coeff *
                (target - state->algorithm.gain_linear);
            io->out.samples[i] = io->in.samples[i] * state->algorithm.gain_linear;
        }
    }
    io->out.buffer->frame_count = ctx->frame_count;
    return orpheus_builtin_gain_emit_out(ctx, &io->out);
}
