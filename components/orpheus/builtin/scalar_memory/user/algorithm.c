#include "component_state.h"
#include "component_api.h"
#include <string.h>

int orpheus_builtin_scalar_memory_reset(orpheus_builtin_scalar_memory_State* s) {
    s->algorithm.value = 0.0f;
    s->algorithm.initialized = false;
    return ORPHEUS_OK;
}

int orpheus_builtin_scalar_memory_prepare(orpheus_builtin_scalar_memory_State* s,
    const OrpheusConfig* config) {
    (void)config;
    return orpheus_builtin_scalar_memory_reset(s);
}

int orpheus_builtin_scalar_memory_set_parameter(orpheus_builtin_scalar_memory_State* s,
    const char* key, const OrpheusValue* value) {
    (void)s;
    (void)value;
    return strcmp(key, "initial_value") ? ORPHEUS_ERR_UNSUPPORTED : ORPHEUS_OK;
}

static bool overlap(const void* a, const void* b) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return (x >= y ? x - y : y - x) < sizeof(float);
}

static bool ready(const OrpheusBuffer* buffer) {
    return buffer->data && buffer->format == ORPHEUS_FORMAT_F32 && buffer->interleaved
        && buffer->channels == 1 && buffer->frame_capacity >= 1 && buffer->frame_count == 1
        && (uintptr_t)buffer->data % _Alignof(float) == 0;
}

int orpheus_builtin_scalar_memory_algorithm(orpheus_builtin_scalar_memory_State* s,
    orpheus_builtin_scalar_memory_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!io->in.buffer) return ORPHEUS_ERR_INVALID_ARG;
    const OrpheusBuffer* inputs[] = {io->in.buffer, io->update.buffer, io->initial.buffer};
    OrpheusBuffer* outputs[] = {io->out.buffer, io->enabled.buffer};
    for (unsigned i = 0; i < 2; ++i)
        if (outputs[i])
            for (unsigned j = 0; j < 3; ++j)
                if (inputs[j] && overlap(outputs[i]->data, inputs[j]->data))
                    return ORPHEUS_ERR_INVALID_ARG;
    if (io->out.buffer && io->enabled.buffer && overlap(io->out.samples, io->enabled.samples))
        return ORPHEUS_ERR_INVALID_ARG;
    int32_t update = io->update.buffer ? io->update.samples[0] : 1;
    float value = s->algorithm.value;
    if (s->write_after_call && !s->algorithm.initialized)
        value = io->initial.buffer ? io->initial.samples[0] : s->initial_value;
    if (!s->write_after_call && update) {
        if (!ready(io->in.buffer)) return ORPHEUS_ERR_INVALID_ARG;
        memcpy(&value, io->in.buffer->data, sizeof(value));
        s->algorithm.value = value;
        s->algorithm.initialized = true;
    }
    if (io->out.buffer) memcpy(io->out.samples, &value, sizeof(value));
    if (io->enabled.buffer) memcpy(io->enabled.samples, &update, sizeof(update));
    int rc = orpheus_builtin_scalar_memory_emit_out(ctx, &io->out);
    if (rc || !s->write_after_call || !update) return rc;
    if (!ready(io->in.buffer)) return ORPHEUS_ERR_INVALID_ARG;
    memcpy(&s->algorithm.value, io->in.buffer->data, sizeof(s->algorithm.value));
    s->algorithm.initialized = true;
    return ORPHEUS_OK;
}
