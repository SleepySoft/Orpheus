#include "component_state.h"
#include <math.h>
#include <string.h>

int orpheus_builtin_saturation_prepare(orpheus_builtin_saturation_State* s, const OrpheusConfig* config) {
    (void)s; (void)config;
    return ORPHEUS_OK;
}

int orpheus_builtin_saturation_reset(orpheus_builtin_saturation_State* s) {
    s->limit = 0.5f;
    s->soft = 0.0f;
    return ORPHEUS_OK;
}

int orpheus_builtin_saturation_set_parameter(orpheus_builtin_saturation_State* s,
        const char* key, const OrpheusValue* value) {
    float* field;
    if (strcmp(key, "limit") == 0) field = &s->limit;
    else if (strcmp(key, "soft") == 0) field = &s->soft;
    else return ORPHEUS_ERR_NOT_FOUND;
    if (value->type != ORPHEUS_VALUE_FLOAT) return ORPHEUS_ERR_INVALID_ARG;
    *field = value->value.f32;
    return ORPHEUS_OK;
}

int orpheus_builtin_saturation_algorithm(orpheus_builtin_saturation_State* s,
        orpheus_builtin_saturation_IO* io, const OrpheusProcessContext* ctx) {
    if (!io->in.buffer || !io->out.buffer) return ORPHEUS_ERR_INVALID_ARG;
    float limit = s->limit > 0.0f ? s->limit : 0.001f;
    float soft = s->soft < 0.0f ? 0.0f : (s->soft > 1.0f ? 1.0f : s->soft);
    for (uint32_t i = 0; i < ctx->frame_count * (uint32_t)s->channels; ++i) {
        float x = io->in.samples[i];
        float hard = x > limit ? limit : (x < -limit ? -limit : x);
        float soft_y = limit * tanhf(x / limit);
        io->out.samples[i] = (1.0f - soft) * hard + soft * soft_y;
    }
    io->out.buffer->frame_count = ctx->frame_count;
    return orpheus_builtin_saturation_emit_out(ctx, &io->out);
}
