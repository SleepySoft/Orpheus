#include "component_state.h"
#include "component_api.h"
#include <math.h>

static float magnitude(float a, float b) {
    float x = fabsf(a), y = fabsf(b), r;
    if (x < y) {
        r = x / y;
        volatile float square = r * r;
        return sqrtf(square + 1.0f) * y;
    }
    if (x > y) {
        r = y / x;
        volatile float square = r * r;
        return sqrtf(square + 1.0f) * x;
    }
    return x * 1.41421354f;
}

int orpheus_builtin_bulk_math_algorithm(orpheus_builtin_bulk_math_State* s,
    orpheus_builtin_bulk_math_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    size_t count = (size_t)s->channels * s->elements, bytes = count * sizeof(float);
    const OrpheusBuffer* inputs[] = {io->a.buffer, io->b.buffer};
    for (unsigned i = 0; i < 2; ++i)
        if (inputs[i] && io->out.buffer) {
            uintptr_t a = (uintptr_t)inputs[i]->data, b = (uintptr_t)io->out.samples;
            if ((a >= b ? a - b : b - a) < bytes) return ORPHEUS_ERR_INVALID_ARG;
        }
    if (io->out.buffer)
        for (size_t i = 0; i < count; ++i) {
            float a = io->a.buffer ? io->a.samples[i] : s->a_default;
            float b = io->b.buffer ? io->b.samples[i] : s->b_default;
            switch (s->operation) {
                case 0: io->out.samples[i] = a + b; break;
                case 1: io->out.samples[i] = a - b; break;
                case 2: io->out.samples[i] = a * b; break;
                case 3: io->out.samples[i] = a / b; break;
                default: io->out.samples[i] = magnitude(a, b); break;
            }
        }
    return orpheus_builtin_bulk_math_emit_out(ctx, &io->out);
}
