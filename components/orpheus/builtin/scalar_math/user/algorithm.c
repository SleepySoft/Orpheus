#include "component_state.h"
#include "component_api.h"
#include <math.h>
#include <string.h>

int orpheus_builtin_scalar_math_algorithm(orpheus_builtin_scalar_math_State* s,
    orpheus_builtin_scalar_math_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    float v[] = {s->a_default, s->b_default, s->c_default};
    const OrpheusBuffer* inputs[] = {io->a.buffer, io->b.buffer, io->c.buffer};
    for (unsigned i = 0; i < 3; ++i) {
        if (!inputs[i]) continue;
        if (io->out.buffer) {
            uintptr_t a = (uintptr_t)inputs[i]->data, b = (uintptr_t)io->out.samples;
            if ((a >= b ? a - b : b - a) < sizeof(float)) return ORPHEUS_ERR_INVALID_ARG;
        }
        memcpy(v + i, inputs[i]->data, sizeof(float));
    }
    float result;
    switch (s->operation) {
        case 0: result = fminf(v[0], v[1]); break;
        case 1: result = fmaxf(v[0], v[1]); break;
        case 2: result = v[0] > v[1] ? 1.0f : 0.0f; break;
        case 3: result = v[0] != v[1] ? 1.0f : 0.0f; break;
        case 4: memcpy(&result, &v[v[0] != 0.0f ? 1 : 2], sizeof(result)); break;
        case 6: result = v[0] + v[1]; break;
        case 7: result = v[0] - v[1]; break;
        case 8: result = v[0] * v[1]; break;
        case 9: result = v[0] / v[1]; break;
        case 10: result = fabsf(v[0]); break;
        case 11: result = log10f(v[0]); break;
        default: memcpy(&result, v, sizeof(result)); break;
    }
    if (io->out.buffer) memcpy(io->out.samples, &result, sizeof(result));
    return orpheus_builtin_scalar_math_emit_out(ctx, &io->out);
}
