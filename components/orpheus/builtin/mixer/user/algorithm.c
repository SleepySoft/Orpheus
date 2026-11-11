#include "component_state.h"
#include "component_api.h"
#include <math.h>
#include <string.h>

static float db_to_linear(float db) {
    return powf(10.0f, db / 20.0f);
}

int orpheus_builtin_mixer_prepare(orpheus_builtin_mixer_State* s, const OrpheusConfig* config) {
    (void)config;
    s->algorithm.gain_linear[0] = s->algorithm.gain_linear[1] = 1.0f;
    s->algorithm.target_gain_linear[0] = db_to_linear(s->gain0);
    s->algorithm.target_gain_linear[1] = db_to_linear(s->gain1);
    return ORPHEUS_OK;
}

int orpheus_builtin_mixer_reset(orpheus_builtin_mixer_State* s) {
    s->gain0 = s->gain1 = 0.0f;
    s->algorithm.gain_linear[0] = s->algorithm.gain_linear[1] = 1.0f;
    s->algorithm.target_gain_linear[0] = s->algorithm.target_gain_linear[1] = 1.0f;
    return ORPHEUS_OK;
}

int orpheus_builtin_mixer_set_parameter(orpheus_builtin_mixer_State* s,
    const char* id, const OrpheusValue* value) {
    if (!strcmp(id, "gain0")) s->algorithm.target_gain_linear[0] = db_to_linear(value->value.f32);
    else if (!strcmp(id, "gain1")) s->algorithm.target_gain_linear[1] = db_to_linear(value->value.f32);
    else return ORPHEUS_ERR_NOT_FOUND;
    return ORPHEUS_OK;
}

int orpheus_builtin_mixer_algorithm(orpheus_builtin_mixer_State* s,
    orpheus_builtin_mixer_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!io->in0.buffer || !io->in1.buffer || !io->out.buffer
        || io->in0.buffer->frame_count < ctx->frame_count || io->in1.buffer->frame_count < ctx->frame_count)
        return ORPHEUS_ERR_INVALID_ARG;
    size_t count = (size_t)ctx->frame_count * s->channels, bytes = count * sizeof(float);
    uintptr_t out = (uintptr_t)io->out.samples;
    const float* inputs[] = {io->in0.samples, io->in1.samples};
    for (unsigned port = 0; port < 2; ++port) {
        uintptr_t in = (uintptr_t)inputs[port];
        if (in >= out ? in - out < bytes : out - in < bytes) return ORPHEUS_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < count; ++i) {
        s->algorithm.gain_linear[0] += 1.0f *
            (s->algorithm.target_gain_linear[0] - s->algorithm.gain_linear[0]);
        s->algorithm.gain_linear[1] += 1.0f *
            (s->algorithm.target_gain_linear[1] - s->algorithm.gain_linear[1]);
        io->out.samples[i] = io->in0.samples[i] * s->algorithm.gain_linear[0]
            + io->in1.samples[i] * s->algorithm.gain_linear[1];
    }
    return orpheus_builtin_mixer_emit_out(ctx, &io->out);
}
