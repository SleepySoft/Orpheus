#include "component_state.h"
#include "component_api.h"

int orpheus_builtin_negate_algorithm(orpheus_builtin_negate_State* s,
    orpheus_builtin_negate_IO* io, const OrpheusProcessContext* ctx) {
    if (!io->in.buffer || !io->out.buffer) return ORPHEUS_ERR_INVALID_ARG;
    for (size_t i = 0; i < (size_t)ctx->frame_count * s->channels; ++i)
        io->out.samples[i] = -io->in.samples[i];
    return orpheus_builtin_negate_emit_out(ctx, &io->out);
}
