#include "component_state.h"
#include "component_api.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int parse_coefficients(const char* text, float* values, int32_t capacity) {
    int32_t count = 0;
    if (values) memset(values, 0, (size_t)capacity * sizeof(float));
    while (isspace((unsigned char)*text)) ++text;
    if (!*text) {
        if (values) values[0] = 1.0f;
        return ORPHEUS_OK;
    }
    for (;;) {
        if (count == capacity) return ORPHEUS_ERR_INVALID_ARG;
        char* end;
        float value = (float)strtod(text, &end);
        if (end == text) return ORPHEUS_ERR_INVALID_ARG;
        if (values) values[count] = value;
        ++count;
        bool spaced = isspace((unsigned char)*end) != 0;
        while (isspace((unsigned char)*end)) ++end;
        if (!*end) return ORPHEUS_OK;
        if (*end == ',') {
            ++end;
            while (isspace((unsigned char)*end)) ++end;
            if (!*end) return ORPHEUS_ERR_INVALID_ARG;
        } else if (!spaced) return ORPHEUS_ERR_INVALID_ARG;
        text = end;
    }
}

int orpheus_builtin_window_validate(const orpheus_builtin_window_Parameters* p) {
    if (strcmp(p->mode, "single") && strcmp(p->mode, "repeat")) return ORPHEUS_ERR_INVALID_ARG;
    return parse_coefficients(p->coefficients, NULL, p->window_size);
}

int orpheus_builtin_window_prepare(orpheus_builtin_window_State* s, const OrpheusConfig* config) {
    (void)config;
    return parse_coefficients(s->coefficients, s->algorithm.coeffs, s->window_size);
}

int orpheus_builtin_window_reset(orpheus_builtin_window_State* s) {
    memset(s->algorithm.coeffs, 0, sizeof(s->algorithm.coeffs));
    s->algorithm.coeffs[0] = 1.0f;
    strcpy(s->mode, "single");
    return ORPHEUS_OK;
}

int orpheus_builtin_window_algorithm(orpheus_builtin_window_State* s,
    orpheus_builtin_window_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!io->in.buffer || !io->out.buffer || io->in.buffer->frame_count < ctx->frame_count)
        return ORPHEUS_ERR_INVALID_ARG;
    size_t bytes = (size_t)ctx->frame_count * s->channels * sizeof(float);
    uintptr_t x = (uintptr_t)io->in.samples, y = (uintptr_t)io->out.samples;
    if (x != y && (x >= y ? x - y < bytes : y - x < bytes)) return ORPHEUS_ERR_INVALID_ARG;
    bool repeat = !strcmp(s->mode, "repeat");
    for (uint32_t f = 0; f < ctx->frame_count; ++f) {
        float weight = repeat ? s->algorithm.coeffs[f % s->window_size]
            : f < (uint32_t)s->window_size ? s->algorithm.coeffs[f] : 1.0f;
        for (int32_t c = 0; c < s->channels; ++c)
            io->out.samples[(size_t)f * s->channels + c] = io->in.samples[(size_t)f * s->channels + c] * weight;
    }
    return orpheus_builtin_window_emit_out(ctx, &io->out);
}
