#include "component_state.h"
#include "component_api.h"
#include <string.h>

int orpheus_test_spectral_gate_prepare(orpheus_test_spectral_gate_State* s, const OrpheusConfig* config) {
    s->algorithm.calls = 0;
    s->algorithm.quantum = config->block_size;
    return s->algorithm.quantum ? ORPHEUS_OK : ORPHEUS_ERR_INVALID_ARG;
}

int orpheus_test_spectral_gate_algorithm(orpheus_test_spectral_gate_State* s,
    orpheus_test_spectral_gate_IO* io, const OrpheusProcessContext* ctx) {
    if (ctx->trigger_input != io->clock.input_index || !io->clock.buffer ||
        !io->spectrum.buffer || !io->out.buffer || ctx->frame_count != s->algorithm.quantum ||
        ctx->frame_index != s->algorithm.calls * s->algorithm.quantum ||
        io->clock.buffer->frame_count != s->algorithm.quantum)
        return ORPHEUS_ERR_INVALID_ARG;
    memcpy(io->out.samples, io->spectrum.samples, (size_t)s->fft_size * s->num_frames * s->channels * sizeof(float));
    ++s->algorithm.calls;
    return orpheus_test_spectral_gate_emit_out(ctx, &io->out);
}
