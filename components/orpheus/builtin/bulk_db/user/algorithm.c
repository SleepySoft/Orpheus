#include "component_state.h"
#include "component_api.h"
#include <math.h>
#include <string.h>

static bool overlap(const void* a, const void* b, size_t bytes) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x >= y ? x - y < bytes : y - x < bytes;
}

int orpheus_builtin_bulk_db_algorithm(orpheus_builtin_bulk_db_State* state,
    orpheus_builtin_bulk_db_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!io->in.buffer) return ORPHEUS_ERR_INVALID_ARG;
    if (!io->out.buffer) return ORPHEUS_OK;
    size_t count = (size_t)state->channels * state->elements;
    if (overlap(io->in.samples, io->out.samples, count * sizeof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    const unsigned char* input = (const unsigned char*)io->in.samples;
    unsigned char* output = (unsigned char*)io->out.samples;
    const float factor = state->quantity ? 20.0f : 10.0f;
    for (size_t i = 0; i < count; ++i) {
        float value;
        memcpy(&value, input + i * sizeof(float), sizeof(float));
        if (state->mode) {
            volatile float exponent = value * 2.30258512f;
            exponent = exponent / factor;
            value = expf(exponent);
        } else {
            volatile float biased = value + state->epsilon;
            value = log10f(biased) * factor;
        }
        memcpy(output + i * sizeof(float), &value, sizeof(float));
    }
    return orpheus_builtin_bulk_db_emit_out(ctx, &io->out);
}
