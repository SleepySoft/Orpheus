#include "component_state.h"
#include "component_api.h"

int orpheus_builtin_interleave_algorithm(orpheus_builtin_interleave_State* s,
    orpheus_builtin_interleave_IO* io, const OrpheusProcessContext* ctx) {
    if (!io->out.samples || io->in_count != (uint32_t)s->channels)
        return ORPHEUS_ERR_INVALID_ARG;
    for (uint32_t ch = 0; ch < io->in_count; ++ch) {
        const float* src = io->in[ch].samples;
        for (uint32_t f = 0; f < ctx->frame_count; ++f)
            io->out.samples[(size_t)f * s->channels + ch] = src ? src[f] : 0.0f;
    }
    return orpheus_builtin_interleave_emit_out(ctx, &io->out);
}
