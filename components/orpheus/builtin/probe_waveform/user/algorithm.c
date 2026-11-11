#include "component_state.h"
#include "component_api.h"
#include <string.h>

int orpheus_builtin_probe_waveform_prepare(orpheus_builtin_probe_waveform_State* s, const OrpheusConfig* config) {
    (void)config;
    s->algorithm.head = 0;
    memset(s->waveform, 0, sizeof(s->waveform));
    return ORPHEUS_OK;
}

int orpheus_builtin_probe_waveform_reset(orpheus_builtin_probe_waveform_State* s) {
    (void)s;
    return ORPHEUS_OK;
}

int orpheus_builtin_probe_waveform_algorithm(orpheus_builtin_probe_waveform_State* s,
    orpheus_builtin_probe_waveform_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!io->in.buffer || io->in.buffer->frame_count < ctx->frame_count) return ORPHEUS_ERR_INVALID_ARG;
    for (uint32_t f = 0; f < ctx->frame_count; ++f) {
        memcpy(s->waveform + s->algorithm.head, io->in.samples + (size_t)f * s->channels, sizeof(float));
        s->algorithm.head = (s->algorithm.head + 1) % 1023;
    }
    if (io->out.buffer)
        memmove(io->out.samples, io->in.samples, (size_t)ctx->frame_count * s->channels * sizeof(float));
    return orpheus_builtin_probe_waveform_emit_out(ctx, &io->out);
}
