#include "component_state.h"
#include "component_api.h"
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static int parse(const char* text, int32_t* map, int32_t count) {
    for (int32_t c = 0; c < count; ++c) {
        char* end;
        errno = 0;
        long number = strtol(text, &end, 10);
        if (end == text || errno == ERANGE || number < INT32_MIN || number > INT32_MAX)
            return ORPHEUS_ERR_INVALID_ARG;
        while (isspace((unsigned char)*end)) ++end;
        if (c + 1 < count ? *end != ',' : *end != '\0') return ORPHEUS_ERR_INVALID_ARG;
        map[c] = (int32_t)number;
        text = c + 1 < count ? end + 1 : end;
    }
    return ORPHEUS_OK;
}

int orpheus_builtin_channel_router_validate(const orpheus_builtin_channel_router_Parameters* p) {
    int32_t map[64];
    if (!p->indices[0]) return ORPHEUS_OK;
    return parse(p->indices, map, p->channels_out);
}

int orpheus_builtin_channel_router_prepare(orpheus_builtin_channel_router_State* s,
    const OrpheusConfig* config) {
    (void)config;
    if (s->indices[0]) return parse(s->indices, s->algorithm.map, s->channels_out);
    for (int32_t c = 0; c < s->channels_out; ++c) s->algorithm.map[c] = c;
    return ORPHEUS_OK;
}

int orpheus_builtin_channel_router_reset(orpheus_builtin_channel_router_State* s) {
    (void)s;
    return ORPHEUS_OK;
}

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x >= y ? x - y < nb : y - x < na;
}

int orpheus_builtin_channel_router_algorithm(orpheus_builtin_channel_router_State* s,
    orpheus_builtin_channel_router_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!io->in.buffer || io->in.buffer->frame_count < ctx->frame_count)
        return ORPHEUS_ERR_INVALID_ARG;
    if (!io->out.buffer) return ORPHEUS_OK;
    if (overlap(io->in.samples, (size_t)ctx->frame_count * s->channels_in * sizeof(float),
        io->out.samples, (size_t)ctx->frame_count * s->channels_out * sizeof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    for (uint32_t n = 0; n < ctx->frame_count; ++n) {
        for (int32_t c = 0; c < s->channels_out; ++c) {
            int32_t index = s->algorithm.map[c];
            float* output = io->out.samples + (size_t)n * s->channels_out + c;
            if (index >= 0 && index < s->channels_in)
                memcpy(output, io->in.samples + (size_t)n * s->channels_in + index, sizeof(float));
            else *output = 0.0f;
        }
    }
    return orpheus_builtin_channel_router_emit_out(ctx, &io->out);
}
