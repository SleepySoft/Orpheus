#include "component_state.h"
#include "component_api.h"

int orpheus_builtin_sample_product_algorithm(orpheus_builtin_sample_product_State* s,
    orpheus_builtin_sample_product_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!io->in.buffer || !io->gain.buffer
        || io->in.buffer->frame_count != ctx->frame_count
        || io->gain.buffer->frame_count != ctx->frame_count)
        return ORPHEUS_ERR_INVALID_ARG;
    if (!io->out.buffer) return ORPHEUS_OK;
    for (size_t i = 0; i < (size_t)ctx->frame_count * s->channels; ++i)
        io->out.samples[i] = io->in.samples[i] * io->gain.samples[i];
    return orpheus_builtin_sample_product_emit_out(ctx, &io->out);
}
