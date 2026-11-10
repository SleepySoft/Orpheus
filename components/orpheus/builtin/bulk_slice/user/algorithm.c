#include "component_state.h"
#include "component_api.h"
#include <string.h>

int orpheus_builtin_bulk_slice_validate(const orpheus_builtin_bulk_slice_Parameters* p) {
    uint64_t count = (uint64_t)p->channels * p->elements;
    uint64_t last = (uint64_t)p->offset + (count - 1) * p->stride;
    return last < (uint64_t)p->input_channels * p->input_elements
        ? ORPHEUS_OK : ORPHEUS_ERR_INVALID_ARG;
}

static bool valid(const OrpheusBuffer* b, uint32_t channels, uint32_t frames, bool input) {
    return b && b->data && b->format == ORPHEUS_FORMAT_F32 && b->interleaved
        && b->channels == channels && b->frame_capacity >= frames
        && (!input || b->frame_count == frames);
}

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x >= y ? x - y < nb : y - x < na;
}

int orpheus_builtin_bulk_slice_algorithm(orpheus_builtin_bulk_slice_State* s,
    orpheus_builtin_bulk_slice_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    const OrpheusBuffer* in = io->in.buffer;
    OrpheusBuffer* out = io->out.buffer;
    if (!valid(in, (uint32_t)s->input_channels, (uint32_t)s->input_elements, true))
        return ORPHEUS_ERR_INVALID_ARG;
    if (!out) return ORPHEUS_OK;
    size_t input_bytes = (size_t)s->input_channels * s->input_elements * sizeof(float);
    size_t count = (size_t)s->channels * s->elements;
    if (!valid(out, (uint32_t)s->channels, (uint32_t)s->elements, false)
        || overlap(in->data, input_bytes, out->data, count * sizeof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    const unsigned char* src = (const unsigned char*)in->data;
    unsigned char* dst = (unsigned char*)out->data;
    for (size_t i = 0; i < count; ++i)
        memcpy(dst + i * sizeof(float),
            src + ((size_t)s->offset + i * s->stride) * sizeof(float), sizeof(float));
    out->frame_count = (uint32_t)s->elements;
    return orpheus_builtin_bulk_slice_emit_out(ctx, &io->out);
}
