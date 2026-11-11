#include "component_state.h"
#include "component_api.h"
#include <math.h>

int orpheus_builtin_device_out_prepare(orpheus_builtin_device_out_State* s, const OrpheusConfig* config) {
    if (!config->block_size || !isfinite(config->sample_rate) || config->sample_rate <= 0 ||
        (s->sample_rate && config->sample_rate != (double)s->sample_rate))
        return ORPHEUS_ERR_INVALID_ARG;
    s->algorithm.sample_rate = config->sample_rate;
    s->algorithm.block_size = config->block_size;
    return ORPHEUS_OK;
}

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return na && nb && (x >= y ? x - y < nb : y - x < na);
}

int orpheus_builtin_device_out_algorithm(orpheus_builtin_device_out_State* s,
    orpheus_builtin_device_out_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->inputs || !ctx->input_count || ctx->trigger_input != io->in.input_index ||
        ctx->sample_rate != s->algorithm.sample_rate) return ORPHEUS_ERR_INVALID_ARG;
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!io->in.buffer || io->in.buffer->frame_count != ctx->frame_count ||
        ctx->frame_count > s->algorithm.block_size) return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes = (size_t)ctx->frame_count * (uint32_t)s->channels * sizeof(float);
    return overlap(io->in.samples, bytes, s, sizeof(*s)) ? ORPHEUS_ERR_INVALID_ARG : ORPHEUS_OK;
}
