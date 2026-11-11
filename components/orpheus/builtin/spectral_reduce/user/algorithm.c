#include "component_state.h"
#include "component_api.h"
#include <float.h>
#include <string.h>

int orpheus_builtin_spectral_reduce_validate(const orpheus_builtin_spectral_reduce_Parameters* p) {
    return !strcmp(p->operation, "sum") || !strcmp(p->operation, "mean")
        || !strcmp(p->operation, "min") || !strcmp(p->operation, "max")
        ? ORPHEUS_OK : ORPHEUS_ERR_INVALID_ARG;
}

int orpheus_builtin_spectral_reduce_algorithm(orpheus_builtin_spectral_reduce_State* s,
    orpheus_builtin_spectral_reduce_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!io->in.buffer || !io->out.buffer || io->in.buffer->frame_count < ctx->frame_count)
        return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes = (size_t)ctx->frame_count * s->channels * sizeof(float);
    uintptr_t x = (uintptr_t)io->in.samples, y = (uintptr_t)io->out.samples;
    if (x >= y ? x - y < bytes : y - x < bytes) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t bins = !s->bin_count || s->bin_count > s->fft_size
        ? (uint32_t)s->fft_size / 2 + 1 : (uint32_t)s->bin_count;
    bool sum = !strcmp(s->operation, "sum"), mean = !strcmp(s->operation, "mean");
    bool minimum = !strcmp(s->operation, "min");
    for (int32_t c = 0; c < s->channels; ++c) {
        float acc = 0.0f, vmin = FLT_MAX, vmax = -FLT_MAX;
        uint32_t count = 0;
        for (int32_t f = 0; f < s->num_frames; ++f) {
            for (uint32_t k = 0; k < bins; ++k) {
                uint32_t frame = (uint32_t)f * s->fft_size + k;
                if (frame >= ctx->frame_count) continue;
                float v = io->in.samples[(size_t)frame * s->channels + c];
                acc += v;
                if (v < vmin) vmin = v;
                if (v > vmax) vmax = v;
                ++count;
            }
        }
        float result = count ? sum ? acc : mean ? acc / (float)count : minimum ? vmin : vmax : 0.0f;
        for (uint32_t f = 0; f < ctx->frame_count; ++f)
            io->out.samples[(size_t)f * s->channels + c] = result;
    }
    return orpheus_builtin_spectral_reduce_emit_out(ctx, &io->out);
}
