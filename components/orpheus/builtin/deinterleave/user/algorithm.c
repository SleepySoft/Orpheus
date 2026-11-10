#include "component_state.h"
#include "component_api.h"

int orpheus_builtin_deinterleave_algorithm(orpheus_builtin_deinterleave_State* s,
    orpheus_builtin_deinterleave_IO* io, const OrpheusProcessContext* ctx) {
    if (!io->in.samples || io->out_count != (uint32_t)s->channels)
        return ORPHEUS_ERR_INVALID_ARG;
    for (uint32_t ch = 0; ch < io->out_count; ++ch) {
        if (!io->out[ch].samples) continue;
        for (uint32_t f = 0; f < ctx->frame_count; ++f)
            io->out[ch].samples[f] = io->in.samples[(size_t)f * s->channels + ch];
        int rc = orpheus_builtin_deinterleave_emit_out(ctx, &io->out[ch]);
        if (rc != ORPHEUS_OK) return rc;
    }
    return ORPHEUS_OK;
}
