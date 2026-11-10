#include "component_state.h"
#include <math.h>
#include <string.h>

static int valid(const orpheus_builtin_exponential_gain_State* s) {
    return s->channels >= 1 && s->channels <= 64 &&
        isfinite(s->target_gain) && s->target_gain >= 0.0f &&
        isfinite(s->ramp_coefficient) && s->ramp_coefficient >= 0.0f &&
        isfinite(s->remaining_blocks) && s->remaining_blocks >= 0.0f &&
        s->remaining_blocks <= 16777216.0f && floorf(s->remaining_blocks) == s->remaining_blocks &&
        isfinite(s->initial_gain) && s->initial_gain >= 0.0f &&
        isfinite(s->min_gain) && s->min_gain > 0.0f &&
        isfinite(s->snap_threshold) && s->snap_threshold >= 0.0f;
}

int orpheus_builtin_exponential_gain_prepare(orpheus_builtin_exponential_gain_State* s,
        const OrpheusConfig* config) {
    (void)config;
    if (!valid(s)) return ORPHEUS_ERR_INVALID_ARG;
    s->current_gain = s->initial_gain;
    s->algorithm.delayed_target = s->algorithm.filter_state = 0.0f;
    s->algorithm.load_initial = true;
    return ORPHEUS_OK;
}

int orpheus_builtin_exponential_gain_reset(orpheus_builtin_exponential_gain_State* s) {
    s->current_gain = s->initial_gain;
    s->algorithm.delayed_target = s->algorithm.filter_state = s->remaining_blocks = 0.0f;
    s->algorithm.load_initial = true;
    return ORPHEUS_OK;
}

int orpheus_builtin_exponential_gain_set_parameter(orpheus_builtin_exponential_gain_State* s,
        const char* key, const OrpheusValue* value) {
    float* field;
    if (strcmp(key, "target_gain") == 0) field = &s->target_gain;
    else if (strcmp(key, "ramp_coefficient") == 0) field = &s->ramp_coefficient;
    else if (strcmp(key, "remaining_blocks") == 0) field = &s->remaining_blocks;
    else return ORPHEUS_ERR_NOT_FOUND;
    if (value->type != ORPHEUS_VALUE_FLOAT || !isfinite(value->value.f32) ||
        value->value.f32 < 0.0f ||
        (field == &s->remaining_blocks &&
         (value->value.f32 > 16777216.0f || floorf(value->value.f32) != value->value.f32)))
        return ORPHEUS_ERR_INVALID_ARG;
    *field = value->value.f32;
    return ORPHEUS_OK;
}

int orpheus_builtin_exponential_gain_algorithm(orpheus_builtin_exponential_gain_State* s,
        orpheus_builtin_exponential_gain_IO* io, const OrpheusProcessContext* ctx) {
    const OrpheusBuffer* in = io->in.buffer;
    OrpheusBuffer* out = io->out.buffer;
    uint32_t frames = ctx->frame_count;
    if (!valid(s) ||
        (in && (!in->data || in->channels != (uint32_t)s->channels || in->frame_capacity < frames)) ||
        (out && (!out->data || out->channels != (uint32_t)s->channels || out->frame_capacity < frames)))
        return ORPHEUS_ERR_INVALID_ARG;
    if (!frames) return orpheus_builtin_exponential_gain_emit_out(ctx, &io->out);
    float current = fmaxf(s->current_gain, s->min_gain);
    bool ramping = fabsf(s->target_gain - current) > s->snap_threshold && s->remaining_blocks > 0.0f;
    if (ramping) {
        s->remaining_blocks -= 1.0f;
        if (s->algorithm.load_initial || s->target_gain - s->algorithm.delayed_target != 0.0f)
            s->algorithm.filter_state = s->ramp_coefficient * current;
    }
    for (uint32_t n = 0; n < frames; ++n) {
        float gain = ramping ? s->algorithm.filter_state : s->target_gain;
        if (ramping) s->algorithm.filter_state = 0.0f - (-s->ramp_coefficient) * gain;
        if (out)
            for (uint32_t c = 0; c < (uint32_t)s->channels; ++c)
                io->out.samples[n * s->channels + c] = (in ? io->in.samples[n * s->channels + c] : 0.0f) * gain;
        s->current_gain = gain;
    }
    if (ramping) {
        s->algorithm.delayed_target = s->target_gain;
        s->algorithm.load_initial = false;
    }
    if (out) out->frame_count = frames;
    return orpheus_builtin_exponential_gain_emit_out(ctx, &io->out);
}
