#include "component_state.h"
#include "component_api.h"
#include <math.h>
#include <string.h>

int orpheus_builtin_scalar_to_i32_algorithm(orpheus_builtin_scalar_to_i32_State* s,
    orpheus_builtin_scalar_to_i32_IO* io, const OrpheusProcessContext* ctx) {
    (void)s;
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!io->in.buffer) return ORPHEUS_ERR_INVALID_ARG;
    float value = io->in.samples[0];
    if (!isfinite(value) || (double)value < -2147483648.0 || (double)value >= 2147483648.0)
        return ORPHEUS_ERR_INVALID_ARG;
    if (!io->out.buffer) return ORPHEUS_OK;
    uintptr_t a = (uintptr_t)io->in.samples, b = (uintptr_t)io->out.samples;
    if ((a > b ? a - b : b - a) < sizeof(int32_t)) return ORPHEUS_ERR_INVALID_ARG;
    int32_t result = (int32_t)value;
    memcpy(io->out.samples, &result, sizeof(result));
    return orpheus_builtin_scalar_to_i32_emit_out(ctx, &io->out);
}
