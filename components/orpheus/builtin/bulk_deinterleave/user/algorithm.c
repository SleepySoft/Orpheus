#include "component_state.h"
#include "component_api.h"
#include <string.h>

static bool valid(const OrpheusBuffer* b, uint32_t channels, uint32_t frames, bool input) {
    return b && b->data && b->format == ORPHEUS_FORMAT_F32 && b->interleaved
        && b->channels == channels && b->frame_capacity >= frames
        && (!input || b->frame_count == frames);
}

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x >= y ? x - y < nb : y - x < na;
}

int orpheus_builtin_bulk_deinterleave_algorithm(orpheus_builtin_bulk_deinterleave_State* s,
    orpheus_builtin_bulk_deinterleave_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    uint32_t channels = (uint32_t)s->channels, elements = (uint32_t)s->elements;
    size_t mono = (size_t)elements * sizeof(float);
    if (io->out_count != channels || !valid(io->in.buffer, channels, elements, true))
        return ORPHEUS_ERR_INVALID_ARG;
    for (uint32_t ch = 0; ch < channels; ++ch) {
        OrpheusBuffer* out = io->out[ch].buffer;
        if (!out) continue;
        if (!valid(out, 1, elements, false)
            || overlap(io->in.buffer->data, mono * channels, out->data, mono))
            return ORPHEUS_ERR_INVALID_ARG;
        for (uint32_t previous = 0; previous < ch; ++previous)
            if (io->out[previous].buffer
                && overlap(io->out[previous].buffer->data, mono, out->data, mono))
                return ORPHEUS_ERR_INVALID_ARG;
    }
    const unsigned char* src = (const unsigned char*)io->in.buffer->data;
    for (uint32_t ch = 0; ch < channels; ++ch) {
        OrpheusBuffer* out = io->out[ch].buffer;
        if (!out) continue;
        for (uint32_t i = 0; i < elements; ++i)
            memcpy((unsigned char*)out->data + (size_t)i * sizeof(float),
                   src + ((size_t)i * channels + ch) * sizeof(float), sizeof(float));
        out->frame_count = elements;
        int rc = orpheus_builtin_bulk_deinterleave_emit_out(ctx, &io->out[ch]);
        if (rc != ORPHEUS_OK) return rc;
    }
    return ORPHEUS_OK;
}
