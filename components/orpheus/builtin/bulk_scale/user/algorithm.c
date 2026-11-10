#include "component_state.h"
#include "component_api.h"
#include <string.h>

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x >= y ? x - y < nb : y - x < na;
}

int orpheus_builtin_bulk_scale_algorithm(orpheus_builtin_bulk_scale_State* state,
    orpheus_builtin_bulk_scale_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!io->in.buffer) return ORPHEUS_ERR_INVALID_ARG;
    if (!io->out.buffer) return ORPHEUS_OK;
    const void* scalar = io->gain.buffer ? io->gain.buffer->data : io->integer_gain.buffer->data;
    size_t count = (size_t)state->channels * state->elements;
    size_t bytes = count * sizeof(float);
    if (overlap(io->in.samples, bytes, io->out.samples, bytes)
        || overlap(scalar, sizeof(float), io->out.samples, bytes))
        return ORPHEUS_ERR_INVALID_ARG;
    float factor;
    if (io->gain.buffer) memcpy(&factor, scalar, sizeof(float));
    else {
        int32_t value;
        memcpy(&value, scalar, sizeof(value));
        factor = (float)value;
    }
    const unsigned char* input = (const unsigned char*)io->in.samples;
    unsigned char* output = (unsigned char*)io->out.samples;
    for (size_t i = 0; i < count; ++i) {
        float value;
        memcpy(&value, input + i * sizeof(float), sizeof(float));
        value *= factor;
        memcpy(output + i * sizeof(float), &value, sizeof(float));
    }
    io->out.buffer->frame_count = (uint32_t)state->elements;
    return orpheus_builtin_bulk_scale_emit_out(ctx, &io->out);
}
