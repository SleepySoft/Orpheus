#include "component_state.h"
#include "component_api.h"
#include <math.h>
#include <string.h>

int orpheus_builtin_device_in_normalize(orpheus_builtin_device_in_Parameters* p, const char* key) {
    (void)key;
    return strcmp(p->source, "microphone") && strcmp(p->source, "loopback")
        ? ORPHEUS_ERR_INVALID_ARG : ORPHEUS_OK;
}

int orpheus_builtin_device_in_reset(orpheus_builtin_device_in_State* s) {
    s->algorithm.frame_index = 0;
    return ORPHEUS_OK;
}

int orpheus_builtin_device_in_prepare(orpheus_builtin_device_in_State* s, const OrpheusConfig* config) {
    if (!config->block_size || !isfinite(config->sample_rate) || config->sample_rate <= 0 ||
        (s->sample_rate && config->sample_rate != (double)s->sample_rate))
        return ORPHEUS_ERR_INVALID_ARG;
    s->algorithm.sample_rate = config->sample_rate;
    s->algorithm.block_size = config->block_size;
    return orpheus_builtin_device_in_reset(s);
}

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return na && nb && (x >= y ? x - y < nb : y - x < na);
}

int orpheus_builtin_device_in_algorithm(orpheus_builtin_device_in_State* s,
    orpheus_builtin_device_in_IO* io, const OrpheusProcessContext* ctx) {
    if (ctx->sample_rate != s->algorithm.sample_rate) return ORPHEUS_ERR_INVALID_ARG;
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!io->out.buffer || ctx->frame_count > s->algorithm.block_size ||
        s->algorithm.frame_index > UINT64_MAX - ctx->frame_count)
        return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes = (size_t)ctx->frame_count * (uint32_t)s->channels * sizeof(float);
    if (overlap(io->out.samples, bytes, s, sizeof(*s))) return ORPHEUS_ERR_INVALID_ARG;
    io->out.delivery.frame_index = s->algorithm.frame_index;
    s->algorithm.frame_index += ctx->frame_count;
    return orpheus_builtin_device_in_emit_out(ctx, &io->out);
}
