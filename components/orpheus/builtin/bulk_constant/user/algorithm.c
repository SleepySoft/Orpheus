#include "component_state.h"
#include "component_api.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int parse(const char* text, float* values, int32_t count) {
    for (int32_t i = 0; i < count; ++i) {
        while (isspace((unsigned char)*text)) ++text;
        char* end;
        errno = 0;
        float value = strtof(text, &end);
        if (end == text || !isfinite(value) || (errno == ERANGE && value == 0.0f))
            return ORPHEUS_ERR_INVALID_ARG;
        if (values) values[i] = value;
        while (isspace((unsigned char)*end)) ++end;
        if (i + 1 < count ? *end != ',' : *end != '\0') return ORPHEUS_ERR_INVALID_ARG;
        text = i + 1 < count ? end + 1 : end;
    }
    return ORPHEUS_OK;
}

int orpheus_builtin_bulk_constant_validate(const orpheus_builtin_bulk_constant_Parameters* p) {
    return parse(p->values, NULL, p->elements);
}

int orpheus_builtin_bulk_constant_prepare(orpheus_builtin_bulk_constant_State* s,
    const OrpheusConfig* config) {
    (void)config;
    return parse(s->values, s->algorithm.values, s->elements);
}

int orpheus_builtin_bulk_constant_reset(orpheus_builtin_bulk_constant_State* s) {
    (void)s;
    return ORPHEUS_OK;
}

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x >= y ? x - y < nb : y - x < na;
}

int orpheus_builtin_bulk_constant_algorithm(orpheus_builtin_bulk_constant_State* s,
    orpheus_builtin_bulk_constant_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    OrpheusBuffer* outputs[] = {io->out.buffer, io->first.buffer, io->last.buffer};
    size_t sizes[] = {(size_t)s->elements * sizeof(float), sizeof(float), sizeof(float)};
    for (unsigned i = 0; i < 3; ++i) {
        if (!outputs[i]) continue;
        if (overlap(outputs[i]->data, sizes[i], s, sizeof(*s))) return ORPHEUS_ERR_INVALID_ARG;
        for (unsigned j = 0; j < i; ++j)
            if (outputs[j] && overlap(outputs[i]->data, sizes[i], outputs[j]->data, sizes[j]))
                return ORPHEUS_ERR_INVALID_ARG;
    }
    for (unsigned i = 0; i < 3; ++i)
        if (outputs[i]) memcpy(outputs[i]->data, s->algorithm.values + (i == 2 ? s->elements - 1 : 0), sizes[i]);
    int rc = orpheus_builtin_bulk_constant_emit_out(ctx, &io->out);
    if (rc) return rc;
    rc = orpheus_builtin_bulk_constant_emit_first(ctx, &io->first);
    return rc ? rc : orpheus_builtin_bulk_constant_emit_last(ctx, &io->last);
}
