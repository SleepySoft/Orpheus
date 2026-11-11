#include "component_state.h"
#include "component_api.h"
#include <string.h>

int orpheus_builtin_audio_bulk_snapshot_algorithm(orpheus_builtin_audio_bulk_snapshot_State* s,
    orpheus_builtin_audio_bulk_snapshot_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!io->in.buffer || io->in.buffer->frame_count != ctx->frame_count
        || (s->elements && (uint32_t)s->elements != ctx->frame_count))
        return ORPHEUS_ERR_INVALID_ARG;
    if (!io->out.buffer) return ORPHEUS_OK;
    size_t bytes = (size_t)ctx->frame_count * s->channels * sizeof(float);
    uintptr_t x = (uintptr_t)io->in.samples, y = (uintptr_t)io->out.samples;
    if (x >= y ? x - y < bytes : y - x < bytes) return ORPHEUS_ERR_INVALID_ARG;
    memcpy(io->out.samples, io->in.samples, bytes);
    return orpheus_builtin_audio_bulk_snapshot_emit_out(ctx, &io->out);
}
