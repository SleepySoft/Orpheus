#include "component_state.h"
#include "component_api.h"
#include <math.h>
#include <string.h>

int orpheus_builtin_sequence_envelope_state_normalize(
    orpheus_builtin_sequence_envelope_state_Parameters* p, const char* key) {
    (void)key;
    return isfinite(p->silent) && p->silent >= 0 ? ORPHEUS_OK : ORPHEUS_ERR_INVALID_ARG;
}

int orpheus_builtin_sequence_envelope_state_reset(orpheus_builtin_sequence_envelope_state_State* s) {
    memset(&s->algorithm, 0, sizeof(s->algorithm));
    return ORPHEUS_OK;
}

int orpheus_builtin_sequence_envelope_state_prepare(orpheus_builtin_sequence_envelope_state_State* s,
    const OrpheusConfig* config) {
    (void)config;
    return orpheus_builtin_sequence_envelope_state_reset(s);
}

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return na && nb && (x >= y ? x - y < nb : y - x < na);
}

int orpheus_builtin_sequence_envelope_state_algorithm(orpheus_builtin_sequence_envelope_state_State* s,
    orpheus_builtin_sequence_envelope_state_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (ctx->trigger_input != io->targets.input_index || !io->targets.buffer ||
        !io->sequence.buffer || !io->coefficient.buffer || !io->feedback.buffer)
        return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes = (size_t)s->channels * sizeof(float);
    const void* inputs[] = {io->targets.samples, io->sequence.samples,
        io->coefficient.samples, io->feedback.samples};
    void* outputs[] = {io->start.samples, io->accepted.samples, io->alpha.samples, io->freeze.samples};
    const size_t sizes[] = {bytes, bytes, sizeof(float), sizeof(int32_t)};
    const size_t input_sizes[] = {bytes, sizeof(int32_t), sizeof(float), bytes};
    for (unsigned i = 0; i < 4; ++i) {
        if (!inputs[i] || overlap(inputs[i], input_sizes[i], s, sizeof(*s)))
            return ORPHEUS_ERR_INVALID_ARG;
        if (!outputs[i]) continue;
        if (overlap(outputs[i], sizes[i], s, sizeof(*s))) return ORPHEUS_ERR_INVALID_ARG;
        for (unsigned j = 0; j < 4; ++j)
            if (overlap(outputs[i], sizes[i], inputs[j], input_sizes[j]))
                return ORPHEUS_ERR_INVALID_ARG;
        for (unsigned j = 0; j < i; ++j)
            if (outputs[j] && overlap(outputs[i], sizes[i], outputs[j], sizes[j]))
                return ORPHEUS_ERR_INVALID_ARG;
    }
    uint32_t sequence;
    memcpy(&sequence, io->sequence.samples, sizeof(sequence));
    int32_t freeze = (sequence & 1U) != 0;
    if (!s->algorithm.initialized) {
        memcpy(s->algorithm.current, io->targets.samples, bytes);
        memcpy(s->algorithm.targets, io->targets.samples, bytes);
        memcpy(&s->algorithm.coefficient, io->coefficient.samples, sizeof(float));
        s->algorithm.initialized = true;
    }
    if (sequence != s->algorithm.sequence && !freeze) {
        for (int32_t i = 0; i < s->channels; ++i)
            s->algorithm.current[i] = fmaxf(s->algorithm.current[i], s->silent);
        memcpy(s->algorithm.targets, io->targets.samples, bytes);
    }
    s->algorithm.sequence = sequence;
    const void* values[] = {s->algorithm.current, s->algorithm.targets, &s->algorithm.coefficient, &freeze};
    for (unsigned i = 0; i < 4; ++i)
        if (outputs[i]) memcpy(outputs[i], values[i], sizes[i]);
    int rc = orpheus_builtin_sequence_envelope_state_emit_accepted(ctx, &io->accepted);
    if (!rc) rc = orpheus_builtin_sequence_envelope_state_emit_alpha(ctx, &io->alpha);
    if (!rc) rc = orpheus_builtin_sequence_envelope_state_emit_freeze(ctx, &io->freeze);
    if (!rc) rc = orpheus_builtin_sequence_envelope_state_emit_start(ctx, &io->start);
    if (rc) return rc;
    const OrpheusBuffer* feedback = io->feedback.buffer;
    if (feedback->frame_count != 1 || feedback->frame_capacity < 1 ||
        feedback->channels != (uint32_t)s->channels || feedback->format != ORPHEUS_FORMAT_F32 ||
        !feedback->interleaved || feedback->data != io->feedback.samples)
        return ORPHEUS_ERR_INVALID_ARG;
    memcpy(s->algorithm.current, io->feedback.samples, bytes);
    return ORPHEUS_OK;
}
