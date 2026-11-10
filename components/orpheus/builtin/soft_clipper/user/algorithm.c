#include "component_state.h"
#include <math.h>
#include <string.h>

int orpheus_builtin_soft_clipper_prepare(orpheus_builtin_soft_clipper_State* s, const OrpheusConfig* config) {
    (void)config;
    s->algorithm.drive_linear = powf(10.0f, s->drive_db / 20.0f);
    s->algorithm.norm = s->algorithm.drive_linear > 0.0001f ? 1.0f / tanhf(s->algorithm.drive_linear) : 1.0f;
    return ORPHEUS_OK;
}

int orpheus_builtin_soft_clipper_reset(orpheus_builtin_soft_clipper_State* s) {
    s->drive_db = 0.0f;
    s->algorithm.drive_linear = 1.0f;
    s->algorithm.norm = 1.0f / tanhf(1.0f);
    return ORPHEUS_OK;
}

int orpheus_builtin_soft_clipper_set_parameter(orpheus_builtin_soft_clipper_State* s,
        const char* key, const OrpheusValue* value) {
    if (strcmp(key, "drive_db") != 0) return ORPHEUS_ERR_NOT_FOUND;
    if (value->type != ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
    s->drive_db = value->value.f32;
    return ORPHEUS_OK;
}

int orpheus_builtin_soft_clipper_algorithm(orpheus_builtin_soft_clipper_State* s,
        orpheus_builtin_soft_clipper_IO* io, const OrpheusProcessContext* ctx) {
    if (!io->in.buffer || !io->out.buffer) return ORPHEUS_ERR_INVALID_ARG;
    float drive = powf(10.0f, s->drive_db / 20.0f);
    float norm = drive > 0.0001f ? 1.0f / tanhf(drive) : 1.0f;
    s->algorithm.drive_linear = drive;
    s->algorithm.norm = norm;
    for (uint32_t i = 0; i < ctx->frame_count * (uint32_t)s->channels; ++i) {
        float x = io->in.samples[i] * drive;
        io->out.samples[i] = tanhf(x) * norm;
    }
    io->out.buffer->frame_count = ctx->frame_count;
    return orpheus_builtin_soft_clipper_emit_out(ctx, &io->out);
}
