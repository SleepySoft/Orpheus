#include "component_state.h"
#include "component_api.h"
#include <stdlib.h>
#include <string.h>

int orpheus_builtin_circular_buffer_normalize(orpheus_builtin_circular_buffer_Parameters* p, const char* key) {
    (void)key;
    return p->hop_size <= p->frame_size ? ORPHEUS_OK : ORPHEUS_ERR_INVALID_ARG;
}

int orpheus_builtin_circular_buffer_reset(orpheus_builtin_circular_buffer_State* s) {
    if (s->algorithm.history)
        memset(s->algorithm.history, 0, (size_t)(s->frame_size - s->hop_size) * s->channels * sizeof(float));
    return ORPHEUS_OK;
}

int orpheus_builtin_circular_buffer_destroy(orpheus_builtin_circular_buffer_State* s) {
    free(s->algorithm.history); free(s->algorithm.scratch);
    s->algorithm.history = NULL; s->algorithm.scratch = NULL;
    return ORPHEUS_OK;
}

int orpheus_builtin_circular_buffer_prepare(orpheus_builtin_circular_buffer_State* s, const OrpheusConfig* config) {
    (void)config;
    size_t history = (size_t)(s->frame_size - s->hop_size) * s->channels;
    size_t input = (size_t)s->hop_size * s->num_frames * s->channels;
    if (history) s->algorithm.history = calloc(history, sizeof(float));
    s->algorithm.scratch = calloc(history + input, sizeof(float));
    return (history && !s->algorithm.history) || !s->algorithm.scratch
        ? ORPHEUS_ERR_OUT_OF_MEMORY : ORPHEUS_OK;
}

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return na && nb && (x >= y ? x - y < nb : y - x < na);
}

int orpheus_builtin_circular_buffer_algorithm(orpheus_builtin_circular_buffer_State* s,
    orpheus_builtin_circular_buffer_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    size_t history = (size_t)(s->frame_size - s->hop_size) * s->channels * sizeof(float);
    size_t input = (size_t)s->hop_size * s->num_frames * s->channels * sizeof(float);
    size_t output = (size_t)s->frame_size * s->num_frames * s->channels * sizeof(float);
    size_t frame = (size_t)s->frame_size * s->channels * sizeof(float);
    size_t hop = (size_t)s->hop_size * s->channels * sizeof(float);
    if (!s->algorithm.scratch || (history && !s->algorithm.history)) return ORPHEUS_ERR_PROCESSING;
    if (ctx->trigger_input != io->in.input_index || !io->in.buffer ||
        overlap(io->in.samples, input, s, sizeof(*s)) ||
        overlap(io->in.samples, input, s->algorithm.history, history) ||
        overlap(io->in.samples, input, s->algorithm.scratch, history + input) ||
        (io->out.samples && (overlap(io->out.samples, output, io->in.samples, input) ||
            overlap(io->out.samples, output, s, sizeof(*s)) ||
            overlap(io->out.samples, output, s->algorithm.history, history) ||
            overlap(io->out.samples, output, s->algorithm.scratch, history + input))))
        return ORPHEUS_ERR_INVALID_ARG;
    if (history) memcpy(s->algorithm.scratch, s->algorithm.history, history);
    memcpy((unsigned char*)s->algorithm.scratch + history, io->in.samples, input);
    if (io->out.samples)
        for (int32_t f = 0; f < s->num_frames; ++f)
            memcpy((unsigned char*)io->out.samples + (size_t)f * frame,
                (unsigned char*)s->algorithm.scratch + (size_t)f * hop, frame);
    if (history) memcpy(s->algorithm.history, (unsigned char*)s->algorithm.scratch + input, history);
    return orpheus_builtin_circular_buffer_emit_out(ctx, &io->out);
}
