#include "component_state.h"
#include <math.h>
#include <string.h>

static void balance_targets(orpheus_builtin_balance_State* s) {
    if (s->balance >= 0.0f) {
        s->algorithm.target_left = 1.0f - s->balance;
        s->algorithm.target_right = 1.0f;
    } else {
        s->algorithm.target_left = 1.0f;
        s->algorithm.target_right = 1.0f + s->balance;
    }
}

int orpheus_builtin_balance_prepare(orpheus_builtin_balance_State* s, const OrpheusConfig* config) {
    balance_targets(s);
    s->algorithm.left_gain = s->algorithm.target_left;
    s->algorithm.right_gain = s->algorithm.target_right;
    s->algorithm.smoothing_coeff = 1.0f;
    if (s->ramp_ms > 0.0f && config->sample_rate != 0) {
        s->algorithm.smoothing_coeff = 1.0f - expf(-1.0f / ((s->ramp_ms / 1000.0f) * (float)config->sample_rate));
        if (s->algorithm.smoothing_coeff > 1.0f) s->algorithm.smoothing_coeff = 1.0f;
    }
    return ORPHEUS_OK;
}

int orpheus_builtin_balance_reset(orpheus_builtin_balance_State* s) {
    s->algorithm.left_gain = s->algorithm.target_left;
    s->algorithm.right_gain = s->algorithm.target_right;
    return ORPHEUS_OK;
}

int orpheus_builtin_balance_set_parameter(orpheus_builtin_balance_State* s,
        const char* key, const OrpheusValue* value) {
    if (strcmp(key, "balance") != 0) return ORPHEUS_ERR_NOT_FOUND;
    s->balance = value->value.f32;
    balance_targets(s);
    return ORPHEUS_OK;
}

int orpheus_builtin_balance_algorithm(orpheus_builtin_balance_State* s,
        orpheus_builtin_balance_IO* io, const OrpheusProcessContext* ctx) {
    if (!io->in.buffer || !io->out.buffer) return ORPHEUS_ERR_INVALID_ARG;
    for (uint32_t n = 0; n < ctx->frame_count; ++n) {
        s->algorithm.left_gain += s->algorithm.smoothing_coeff * (s->algorithm.target_left - s->algorithm.left_gain);
        s->algorithm.right_gain += s->algorithm.smoothing_coeff * (s->algorithm.target_right - s->algorithm.right_gain);
        for (uint32_t c = 0; c < (uint32_t)s->channels; ++c) {
            float gain = c % 2u == 0u ? s->algorithm.left_gain : s->algorithm.right_gain;
            io->out.samples[n * s->channels + c] = io->in.samples[n * s->channels + c] * gain;
        }
    }
    io->out.buffer->frame_count = ctx->frame_count;
    return orpheus_builtin_balance_emit_out(ctx, &io->out);
}
