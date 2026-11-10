#include "component_state.h"
#include "component_api.h"
#include <string.h>

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x >= y ? x - y < nb : y - x < na;
}

int orpheus_builtin_frame_energy_algorithm(orpheus_builtin_frame_energy_State* state,
    orpheus_builtin_frame_energy_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!io->in.buffer || io->in.buffer->frame_count != (uint32_t)state->frames)
        return ORPHEUS_ERR_INVALID_ARG;
    if (!io->out.buffer) return ORPHEUS_OK;
    if (overlap(io->in.samples, (size_t)state->channels * state->frames * sizeof(float),
        io->out.samples, sizeof(float))) return ORPHEUS_ERR_INVALID_ARG;
    const unsigned char* input = (const unsigned char*)io->in.samples;
    volatile float total = -0.0f;
    for (int32_t frame = 0; frame < state->frames; ++frame) {
        size_t base = (size_t)frame * state->channels;
        float value;
        memcpy(&value, input + base * sizeof(float), sizeof(float));
        volatile float row = value * value;
        for (int32_t channel = 1; channel < state->channels; ++channel) {
            memcpy(&value, input + (base + channel) * sizeof(float), sizeof(float));
            volatile float squared = value * value;
            row = row + squared;
        }
        total = total + row;
    }
    float result = total;
    memcpy(io->out.samples, &result, sizeof(float));
    return orpheus_builtin_frame_energy_emit_out(ctx, &io->out);
}
