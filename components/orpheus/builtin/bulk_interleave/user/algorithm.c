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

int orpheus_builtin_bulk_interleave_algorithm(orpheus_builtin_bulk_interleave_State* s,
    orpheus_builtin_bulk_interleave_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    uint32_t channels = (uint32_t)s->channels, elements = (uint32_t)s->elements;
    size_t mono = (size_t)elements * sizeof(float);
    OrpheusBuffer* out = io->out.buffer;
    if (io->in_count != channels || (out && !valid(out, channels, elements, false)))
        return ORPHEUS_ERR_INVALID_ARG;
    for (uint32_t ch = 0; ch < channels; ++ch) {
        const OrpheusBuffer* in = io->in[ch].buffer;
        if (in && (!valid(in, 1, elements, true)
            || (out && overlap(in->data, mono, out->data, mono * channels))))
            return ORPHEUS_ERR_INVALID_ARG;
    }
    if (!out) return ORPHEUS_OK;
    unsigned char* dst = (unsigned char*)out->data;
    for (uint32_t ch = 0; ch < channels; ++ch) {
        const OrpheusBuffer* in = io->in[ch].buffer;
        for (uint32_t i = 0; i < elements; ++i) {
            void* target = dst + ((size_t)i * channels + ch) * sizeof(float);
            if (in)
                memcpy(target, (const unsigned char*)in->data + (size_t)i * sizeof(float), sizeof(float));
            else
                memset(target, 0, sizeof(float));
        }
    }
    out->frame_count = elements;
    return orpheus_builtin_bulk_interleave_emit_out(ctx, &io->out);
}
