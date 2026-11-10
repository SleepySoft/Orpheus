#include "component_state.h"
#include "component_api.h"

int orpheus_builtin_constant_source_algorithm(orpheus_builtin_constant_source_State* state,
    orpheus_builtin_constant_source_IO* io, const OrpheusProcessContext* ctx) {
    if (!io->out.buffer || (ctx->frame_count && !io->out.samples))
        return ORPHEUS_ERR_INVALID_ARG;
    const size_t count = (size_t)ctx->frame_count * io->out.buffer->channels;
    for (size_t i = 0; i < count; ++i) io->out.samples[i] = state->value;
    io->out.buffer->frame_count = ctx->frame_count;
    return orpheus_builtin_constant_source_emit_out(ctx, &io->out);
}
