#include "component_state.h"
#include "component_api.h"
#include <math.h>

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x >= y ? x - y < nb : y - x < na;
}

int orpheus_builtin_complex_power_algorithm(orpheus_builtin_complex_power_State* s,
    orpheus_builtin_complex_power_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!io->in.buffer) return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes = (size_t)s->bins * s->batches * s->channels * sizeof(float);
    size_t energy_bytes = (size_t)s->batches * s->channels * sizeof(float);
    if ((io->power.buffer && overlap(io->in.samples, bytes * 2, io->power.samples, bytes))
        || (io->energy.buffer && overlap(io->in.samples, bytes * 2, io->energy.samples, energy_bytes))
        || (io->power.buffer && io->energy.buffer
            && overlap(io->power.samples, bytes, io->energy.samples, energy_bytes)))
        return ORPHEUS_ERR_INVALID_ARG;
    for (int32_t h = 0; h < s->batches; ++h)
        for (int32_t c = 0; c < s->channels; ++c) {
            float sum = -0.0f;
            for (int32_t k = 0; k < s->bins; ++k) {
                size_t point = (size_t)h * s->bins + k, index = point * 2 * s->channels + c;
                volatile float real = io->in.samples[index] * io->in.samples[index];
                volatile float imaginary = io->in.samples[index + s->channels] * io->in.samples[index + s->channels];
                float value = real + imaginary;
                if (io->power.buffer) io->power.samples[point * s->channels + c] = value;
                sum += value;
            }
            if (io->energy.buffer)
                io->energy.samples[(size_t)h * s->channels + c] = log10f(sum + s->epsilon) * 10.0f;
        }
    int rc = orpheus_builtin_complex_power_emit_power(ctx, &io->power);
    return rc ? rc : orpheus_builtin_complex_power_emit_energy(ctx, &io->energy);
}
