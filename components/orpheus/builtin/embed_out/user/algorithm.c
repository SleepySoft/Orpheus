#include "component_state.h"
#include "component_api.h"
#include <string.h>

int orpheus_builtin_embed_out_normalize(orpheus_builtin_embed_out_Parameters* p, const char* key) {
    return !key && p->valid ? ORPHEUS_ERR_INVALID_ARG : ORPHEUS_OK;
}

int orpheus_builtin_embed_out_reset(orpheus_builtin_embed_out_State* s) {
    memset(s->algorithm.data, 0, sizeof(s->algorithm.data));
    s->valid = false;
    return ORPHEUS_OK;
}

int orpheus_builtin_embed_out_prepare(orpheus_builtin_embed_out_State* s, const OrpheusConfig* config) {
    if (config->sample_rate != (double)s->sample_rate) return ORPHEUS_ERR_INVALID_ARG;
    return orpheus_builtin_embed_out_reset(s);
}

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return na && nb && (x >= y ? x - y < nb : y - x < na);
}

int orpheus_builtin_embed_out_algorithm(orpheus_builtin_embed_out_State* s,
    orpheus_builtin_embed_out_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->inputs || !ctx->input_count || ctx->trigger_input != io->in.input_index ||
        ctx->sample_rate != (double)s->sample_rate) return ORPHEUS_ERR_INVALID_ARG;
    if (!ctx->frame_count) return ORPHEUS_OK;
    uint32_t frames = s->_orpheus_config_frames;
    if (ctx->frame_count != frames || !io->in.buffer || io->in.buffer->frame_count != frames ||
        io->in.buffer->frame_capacity < frames) return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes = (size_t)frames * (uint32_t)s->channels * sizeof(float);
    if (overlap(io->in.samples, bytes, s, sizeof(*s))) return ORPHEUS_ERR_INVALID_ARG;
    memcpy(s->algorithm.data, io->in.samples, bytes);
    s->valid = true;
    return ORPHEUS_OK;
}
