#include "component_state.h"
#include "component_api.h"

static bool overlap(const void* a, const void* b, size_t bytes) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x >= y ? x - y < bytes : y - x < bytes;
}

int orpheus_builtin_bulk_subtract_algorithm(orpheus_builtin_bulk_subtract_State* state,
    orpheus_builtin_bulk_subtract_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!io->a.buffer || !io->b.buffer) return ORPHEUS_ERR_INVALID_ARG;
    if (!io->out.buffer) return ORPHEUS_OK;
    size_t count = (size_t)state->channels * state->elements;
    size_t bytes = count * sizeof(float);
    if (overlap(io->a.samples, io->out.samples, bytes)
        || overlap(io->b.samples, io->out.samples, bytes))
        return ORPHEUS_ERR_INVALID_ARG;
    for (size_t i = 0; i < count; ++i)
        io->out.samples[i] = io->a.samples[i] - io->b.samples[i];
    io->out.buffer->frame_count = (uint32_t)state->elements;
    return orpheus_builtin_bulk_subtract_emit_out(ctx, &io->out);
}
