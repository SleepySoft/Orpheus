#include "component_state.h"
#include "component_api.h"
#include <math.h>
#include <string.h>

static bool overlap(const void* out, const void* input, size_t bytes) {
    uintptr_t a = (uintptr_t)out, b = (uintptr_t)input;
    return a >= b ? a - b < bytes : b - a < sizeof(float);
}

int orpheus_builtin_bulk_lookup_algorithm(orpheus_builtin_bulk_lookup_State* s,
    orpheus_builtin_bulk_lookup_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    const OrpheusBuffer* inputs[] = {io->x.buffer, io->axis.buffer, io->values.buffer};
    size_t counts[] = {1, (size_t)s->points, (size_t)s->points};
    for (unsigned i = 0; i < 3; ++i)
        if (!inputs[i] || (io->out.buffer && overlap(io->out.samples, inputs[i]->data, counts[i] * sizeof(float))))
            return ORPHEUS_ERR_INVALID_ARG;
    const float* axis = io->axis.samples;
    const float* values = io->values.samples;
    float previous = axis[0];
    bool descending = axis[1] < previous;
    if (!isfinite(previous)) return ORPHEUS_ERR_INVALID_ARG;
    for (int32_t i = 1; i < s->points; ++i) {
        float current = axis[i];
        if (!isfinite(current) || (descending ? current >= previous : current <= previous))
            return ORPHEUS_ERR_INVALID_ARG;
        previous = current;
    }
    float x = io->x.samples[0], result = 0.0f;
    int32_t first = descending ? s->points - 1 : 0, last = descending ? 0 : s->points - 1;
    if (x >= axis[first] && x <= axis[last]) {
        int32_t low = 0, high = s->points - 1;
        while (high - low > 1) {
            int32_t mid = (low + high) / 2, at = descending ? s->points - 1 - mid : mid;
            if (x >= axis[at]) low = mid;
            else high = mid;
        }
        int32_t left = descending ? s->points - 1 - low : low;
        int32_t right = descending ? s->points - 1 - high : high;
        float a = axis[left], t = (x - a) / (axis[right] - a);
        float y0 = values[left], y1 = values[right];
        if (t == 0.0f) memcpy(&result, &y0, sizeof(result));
        else if (t == 1.0f) memcpy(&result, &y1, sizeof(result));
        else if (y0 == y1) memcpy(&result, &y0, sizeof(result));
        else {
            volatile float left_value = (1.0f - t) * y0, right_value = t * y1;
            result = left_value + right_value;
        }
    }
    if (io->out.buffer) memcpy(io->out.samples, &result, sizeof(result));
    return orpheus_builtin_bulk_lookup_emit_out(ctx, &io->out);
}
