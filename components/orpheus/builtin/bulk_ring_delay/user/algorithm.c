#include "component_state.h"
#include "component_api.h"
#include <stdlib.h>
#include <string.h>

int orpheus_builtin_bulk_ring_delay_validate(const orpheus_builtin_bulk_ring_delay_Parameters* p) {
    return p->capacity_elements < p->bins ? ORPHEUS_ERR_INVALID_ARG : ORPHEUS_OK;
}

int orpheus_builtin_bulk_ring_delay_prepare(orpheus_builtin_bulk_ring_delay_State* s,
    const OrpheusConfig* config) {
    (void)config;
    size_t count = (size_t)s->capacity_elements * s->channels;
    if (count > SIZE_MAX / sizeof(float)) return ORPHEUS_ERR_OUT_OF_MEMORY;
    float* history = (float*)calloc(count, sizeof(float));
    if (!history) return ORPHEUS_ERR_OUT_OF_MEMORY;
    free(s->algorithm.history);
    s->algorithm.history = history;
    s->algorithm.count = count;
    s->algorithm.cursor = 0;
    return ORPHEUS_OK;
}

int orpheus_builtin_bulk_ring_delay_reset(orpheus_builtin_bulk_ring_delay_State* s) {
    if (s->algorithm.history) memset(s->algorithm.history, 0, s->algorithm.count * sizeof(float));
    s->algorithm.cursor = 0;
    return ORPHEUS_OK;
}

int orpheus_builtin_bulk_ring_delay_destroy(orpheus_builtin_bulk_ring_delay_State* s) {
    free(s->algorithm.history);
    s->algorithm.history = NULL;
    s->algorithm.count = 0;
    s->algorithm.cursor = 0;
    return ORPHEUS_OK;
}

int orpheus_builtin_bulk_ring_delay_algorithm(orpheus_builtin_bulk_ring_delay_State* s,
    orpheus_builtin_bulk_ring_delay_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!s->algorithm.history || !io->in.buffer) return ORPHEUS_ERR_INVALID_ARG;
    size_t row = (size_t)s->channels * sizeof(float), bytes = (size_t)s->bins * s->batches * row;
    if (io->out.buffer) {
        uintptr_t input = (uintptr_t)io->in.samples, output = (uintptr_t)io->out.samples;
        if (input >= output ? input - output < bytes : output - input < bytes)
            return ORPHEUS_ERR_INVALID_ARG;
    }
    for (int32_t h = 0; h < s->batches; ++h) {
        size_t first = s->capacity_elements - s->algorithm.cursor;
        if (first > (size_t)s->bins) first = s->bins;
        memcpy(s->algorithm.history + (size_t)s->algorithm.cursor * s->channels,
            io->in.samples + (size_t)h * s->bins * s->channels, first * row);
        memcpy(s->algorithm.history, io->in.samples + ((size_t)h * s->bins + first) * s->channels,
            (s->bins - first) * row);
        if (io->out.buffer) {
            uint32_t read = (s->algorithm.cursor + s->capacity_elements
                - (uint32_t)s->delay_elements % s->capacity_elements) % s->capacity_elements;
            size_t head = s->capacity_elements - read;
            if (head > (size_t)s->bins) head = s->bins;
            float* output = io->out.samples + (size_t)h * s->bins * s->channels;
            memcpy(output, s->algorithm.history + (size_t)read * s->channels, head * row);
            memcpy(output + head * s->channels, s->algorithm.history, (s->bins - head) * row);
        }
        s->algorithm.cursor = (s->algorithm.cursor + s->bins) % s->capacity_elements;
    }
    return orpheus_builtin_bulk_ring_delay_emit_out(ctx, &io->out);
}
