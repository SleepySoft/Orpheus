#include "component_state.h"
#include "component_api.h"
#include <string.h>

int orpheus_builtin_audio_sample_validate(const orpheus_builtin_audio_sample_Parameters* p) {
    return p->frame < p->frames ? ORPHEUS_OK : ORPHEUS_ERR_INVALID_ARG;
}

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x >= y ? x - y < nb : y - x < na;
}

int orpheus_builtin_audio_sample_algorithm(orpheus_builtin_audio_sample_State* s,
    orpheus_builtin_audio_sample_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!io->in.buffer || io->in.buffer->frame_count != (uint32_t)s->frames
        || io->out_count != (uint32_t)s->channels)
        return ORPHEUS_ERR_INVALID_ARG;
    size_t input_bytes = (size_t)s->frames * s->channels * sizeof(float);
    for (uint32_t ch = 0; ch < io->out_count; ++ch) {
        const OrpheusBuffer* out = io->out[ch].buffer;
        if (!out) continue;
        if (overlap(io->in.samples, input_bytes, out->data, sizeof(float)))
            return ORPHEUS_ERR_INVALID_ARG;
        for (uint32_t previous = 0; previous < ch; ++previous)
            if (io->out[previous].buffer && overlap(io->out[previous].samples,
                sizeof(float), out->data, sizeof(float)))
                return ORPHEUS_ERR_INVALID_ARG;
    }
    for (uint32_t ch = 0; ch < io->out_count; ++ch) {
        if (!io->out[ch].buffer) continue;
        memcpy(io->out[ch].samples, io->in.samples + (size_t)s->frame * s->channels + ch,
            sizeof(float));
        int result = orpheus_builtin_audio_sample_emit_out(ctx, &io->out[ch]);
        if (result != ORPHEUS_OK) return result;
    }
    return ORPHEUS_OK;
}
