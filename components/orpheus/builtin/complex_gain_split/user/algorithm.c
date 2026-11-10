#include "component_state.h"
#include "component_api.h"

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x >= y ? x - y < nb : y - x < na;
}

int orpheus_builtin_complex_gain_split_algorithm(orpheus_builtin_complex_gain_split_State* s,
    orpheus_builtin_complex_gain_split_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!io->in.buffer || !io->gain.buffer) return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes = (size_t)s->elements * s->channels * 2 * sizeof(float);
    OrpheusBuffer* outputs[] = {io->impulse.buffer, io->reverb.buffer};
    for (unsigned i = 0; i < 2; ++i)
        if (outputs[i] && (overlap(io->in.samples, bytes, outputs[i]->data, bytes)
            || overlap(io->gain.samples, bytes / 2, outputs[i]->data, bytes)))
            return ORPHEUS_ERR_INVALID_ARG;
    if (io->impulse.buffer && io->reverb.buffer
        && overlap(io->impulse.samples, bytes, io->reverb.samples, bytes))
        return ORPHEUS_ERR_INVALID_ARG;
    for (int32_t n = 0; n < s->elements; ++n)
        for (int32_t part = 0; part < 2; ++part)
            for (int32_t c = 0; c < s->channels; ++c) {
                size_t index = ((size_t)n * 2 + part) * s->channels + c;
                volatile float product = io->gain.samples[(size_t)n * s->channels + c] * io->in.samples[index];
                if (io->reverb.buffer) io->reverb.samples[index] = product;
                if (io->impulse.buffer) io->impulse.samples[index] = io->in.samples[index] - product;
            }
    int rc = orpheus_builtin_complex_gain_split_emit_impulse(ctx, &io->impulse);
    return rc ? rc : orpheus_builtin_complex_gain_split_emit_reverb(ctx, &io->reverb);
}
