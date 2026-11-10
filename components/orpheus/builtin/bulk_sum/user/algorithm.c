#include "component_state.h"
#include "component_api.h"
#include <string.h>

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x >= y ? x - y < nb : y - x < na;
}

int orpheus_builtin_bulk_sum_algorithm(orpheus_builtin_bulk_sum_State* state,
    orpheus_builtin_bulk_sum_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!io->in.buffer) return ORPHEUS_ERR_INVALID_ARG;
    if (!io->out.buffer) return ORPHEUS_OK;
    size_t output_bytes = (size_t)state->channels * sizeof(float);
    if (overlap(io->in.samples, output_bytes * state->elements, io->out.samples, output_bytes))
        return ORPHEUS_ERR_INVALID_ARG;
    const unsigned char* input = (const unsigned char*)io->in.samples;
    unsigned char* output = (unsigned char*)io->out.samples;
    if (state->elements == 1) memcpy(output, input, output_bytes);
    else for (int32_t channel = 0; channel < state->channels; ++channel) {
        float value;
        memcpy(&value, input + (size_t)channel * sizeof(float), sizeof(float));
        volatile float sum = value;
        for (int32_t i = 1; i < state->elements; ++i) {
            memcpy(&value, input + ((size_t)i * state->channels + channel) * sizeof(float),
                sizeof(float));
            sum = sum + value;
        }
        value = sum;
        memcpy(output + (size_t)channel * sizeof(float), &value, sizeof(float));
    }
    io->out.buffer->frame_count = 1;
    return orpheus_builtin_bulk_sum_emit_out(ctx, &io->out);
}
