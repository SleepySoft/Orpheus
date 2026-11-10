#include "component_state.h"

int orpheus_builtin_square_prepare(orpheus_builtin_square_State* s, const OrpheusConfig* config) {
    (void)s; (void)config;
    return ORPHEUS_OK;
}

int orpheus_builtin_square_reset(orpheus_builtin_square_State* s) {
    s->channels = 2;
    return ORPHEUS_OK;
}

int orpheus_builtin_square_set_parameter(orpheus_builtin_square_State* s,
        const char* key, const OrpheusValue* value) {
    (void)s; (void)key; (void)value;
    return ORPHEUS_ERR_NOT_FOUND;
}

int orpheus_builtin_square_algorithm(orpheus_builtin_square_State* s,
        orpheus_builtin_square_IO* io, const OrpheusProcessContext* ctx) {
    if (!io->in.buffer || !io->out.buffer) return ORPHEUS_ERR_INVALID_ARG;
    for (uint32_t i = 0; i < ctx->frame_count * (uint32_t)s->channels; ++i)
        io->out.samples[i] = io->in.samples[i] * io->in.samples[i];
    io->out.buffer->frame_count = ctx->frame_count;
    return orpheus_builtin_square_emit_out(ctx, &io->out);
}
