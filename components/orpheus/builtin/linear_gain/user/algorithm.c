#include "component_state.h"
#include "component_api.h"

int orpheus_builtin_linear_gain_algorithm(orpheus_builtin_linear_gain_State* state,
    orpheus_builtin_linear_gain_IO* io, const OrpheusProcessContext* ctx) {
    if (!io->in.buffer || !io->out.buffer
        || (ctx->frame_count && (!io->in.samples || !io->out.samples))
        || io->in.buffer->frame_count < ctx->frame_count
        || io->in.buffer->channels != io->out.buffer->channels)
        return ORPHEUS_ERR_INVALID_ARG;
    const size_t count = (size_t)ctx->frame_count * io->out.buffer->channels;
    for (size_t i = 0; i < count; ++i)
        io->out.samples[i] = io->in.samples[i] * state->gain;
    io->out.buffer->frame_count = ctx->frame_count;
    return orpheus_builtin_linear_gain_emit_out(ctx, &io->out);
}
