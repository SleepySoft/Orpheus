#include "component_state.h"
#include "component_api.h"
#include <stdlib.h>
#include <string.h>

int orpheus_builtin_overlap_add_normalize(orpheus_builtin_overlap_add_Parameters* p, const char* key) {
    (void)key;
    return p->hop_size <= p->frame_size ? ORPHEUS_OK : ORPHEUS_ERR_INVALID_ARG;
}

int orpheus_builtin_overlap_add_reset(orpheus_builtin_overlap_add_State* s) {
    if (s->algorithm.history)
        memset(s->algorithm.history, 0, (size_t)s->frame_size * s->channels * sizeof(float));
    return ORPHEUS_OK;
}

int orpheus_builtin_overlap_add_destroy(orpheus_builtin_overlap_add_State* s) {
    free(s->algorithm.history); s->algorithm.history = NULL;
    return ORPHEUS_OK;
}

int orpheus_builtin_overlap_add_prepare(orpheus_builtin_overlap_add_State* s, const OrpheusConfig* config) {
    (void)config;
    s->algorithm.history = calloc((size_t)s->frame_size * s->channels, sizeof(float));
    return s->algorithm.history ? ORPHEUS_OK : ORPHEUS_ERR_OUT_OF_MEMORY;
}

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return na && nb && (x >= y ? x - y < nb : y - x < na);
}

int orpheus_builtin_overlap_add_algorithm(orpheus_builtin_overlap_add_State* s,
    orpheus_builtin_overlap_add_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!s->algorithm.history) return ORPHEUS_ERR_PROCESSING;
    size_t frame = (size_t)s->frame_size * s->channels;
    size_t hop = (size_t)s->hop_size * s->channels;
    size_t input_bytes = frame * s->num_frames * sizeof(float);
    size_t output_bytes = hop * s->num_frames * sizeof(float);
    if (ctx->trigger_input != io->in.input_index || !io->in.buffer ||
        overlap(io->in.samples, input_bytes, s, sizeof(*s)) ||
        overlap(io->in.samples, input_bytes, s->algorithm.history, frame * sizeof(float)) ||
        (io->out.samples && (overlap(io->out.samples, output_bytes, io->in.samples, input_bytes) ||
            overlap(io->out.samples, output_bytes, s, sizeof(*s)) ||
            overlap(io->out.samples, output_bytes, s->algorithm.history, frame * sizeof(float)))))
        return ORPHEUS_ERR_INVALID_ARG;
    for (int32_t f = 0; f < s->num_frames; ++f) {
        const float* src = io->in.samples + (size_t)f * frame;
        for (size_t i = 0; i < frame; ++i) s->algorithm.history[i] += src[i];
        if (io->out.samples) memcpy(io->out.samples + (size_t)f * hop, s->algorithm.history, hop * sizeof(float));
        if (frame > hop) memmove(s->algorithm.history, s->algorithm.history + hop, (frame - hop) * sizeof(float));
        memset(s->algorithm.history + frame - hop, 0, hop * sizeof(float));
    }
    return orpheus_builtin_overlap_add_emit_out(ctx, &io->out);
}
