#include "component_state.h"
#include "component_api.h"
#include <string.h>

int orpheus_builtin_complementary_coefficients_algorithm(orpheus_builtin_complementary_coefficients_State* s,
    orpheus_builtin_complementary_coefficients_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!io->in.buffer) return ORPHEUS_ERR_INVALID_ARG;
    if (io->out.buffer) {
        size_t bytes = (size_t)s->channels * s->elements * sizeof(float);
        uintptr_t input = (uintptr_t)io->in.samples, output = (uintptr_t)io->out.samples;
        if (input >= output ? input - output < bytes * 2 : output - input < bytes)
            return ORPHEUS_ERR_INVALID_ARG;
        for (int32_t row = 0; row < s->elements; ++row) {
            size_t base = (size_t)row * s->channels;
            memcpy(io->out.samples + base * 2, io->in.samples + base, s->channels * sizeof(float));
            for (int32_t c = 0; c < s->channels; ++c)
                io->out.samples[base * 2 + s->channels + c] = s->total - io->in.samples[base + c];
        }
    }
    return orpheus_builtin_complementary_coefficients_emit_out(ctx, &io->out);
}
