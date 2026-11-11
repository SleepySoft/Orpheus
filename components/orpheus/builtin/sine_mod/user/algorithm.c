#include "component_state.h"
#include "component_api.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TWO_PI 6.283185307179586f

static float phase_increment(float frequency, float rate) {
    return TWO_PI * frequency / (rate > 0 ? rate : 48000.0f);
}

static int parse_table(const char* cursor, unsigned count, float* table) {
    if (!cursor) return ORPHEUS_ERR_INVALID_ARG;
    for (unsigned n = 0; n < count; ++n) {
        char* end = NULL;
        errno = 0;
        float value = strtof(cursor, &end);
        if (end == cursor || errno == ERANGE || !isfinite(value)) return ORPHEUS_ERR_INVALID_ARG;
        if (table) table[n] = value;
        while (isspace((unsigned char)*end)) ++end;
        if (n + 1 == count ? *end != '\0' : *end != ',') return ORPHEUS_ERR_INVALID_ARG;
        cursor = n + 1 == count ? end : end + 1;
    }
    return ORPHEUS_OK;
}

int orpheus_builtin_sine_mod_validate(const orpheus_builtin_sine_mod_Parameters* p) {
    return p->modulation_mode == 1 ? parse_table(p->carrier_table, p->table_length, NULL) : ORPHEUS_OK;
}

int orpheus_builtin_sine_mod_prepare(orpheus_builtin_sine_mod_State* s, const OrpheusConfig* config) {
    if (s->modulation_mode == 1) {
        int rc = parse_table(s->carrier_table, s->table_length, s->algorithm.carrier_table);
        if (rc) return rc;
    }
    s->algorithm.sample_rate = config->sample_rate > 0 ? (float)config->sample_rate : 48000.0f;
    s->algorithm.phase_inc = phase_increment(s->freq_hz, s->algorithm.sample_rate);
    s->algorithm.phase = 0.0f;
    s->algorithm.table_index = 0;
    return ORPHEUS_OK;
}

int orpheus_builtin_sine_mod_reset(orpheus_builtin_sine_mod_State* s) {
    s->algorithm.phase = 0.0f;
    s->algorithm.table_index = 0;
    if (s->modulation_mode == 0) {
        s->freq_hz = s->depth = 1.0f;
        s->algorithm.phase_inc = phase_increment(s->freq_hz, s->algorithm.sample_rate);
    }
    return ORPHEUS_OK;
}

int orpheus_builtin_sine_mod_set_parameter(orpheus_builtin_sine_mod_State* s,
    const char* id, const OrpheusValue* value) {
    if (!strcmp(id, "freq_hz"))
        s->algorithm.phase_inc = phase_increment(value->value.f32, s->algorithm.sample_rate);
    else if (strcmp(id, "depth")) return ORPHEUS_ERR_NOT_FOUND;
    return ORPHEUS_OK;
}

int orpheus_builtin_sine_mod_algorithm(orpheus_builtin_sine_mod_State* s,
    orpheus_builtin_sine_mod_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!io->in.buffer || !io->out.buffer || io->in.buffer->frame_count < ctx->frame_count)
        return ORPHEUS_ERR_INVALID_ARG;
    size_t count = (size_t)ctx->frame_count * s->channels, bytes = count * sizeof(float);
    uintptr_t x = (uintptr_t)io->in.samples, y = (uintptr_t)io->out.samples;
    if (x != y && (x >= y ? x - y < bytes : y - x < bytes)) return ORPHEUS_ERR_INVALID_ARG;
    if (s->modulation_mode == 1) {
        for (uint32_t n = 0; n < ctx->frame_count; ++n) {
            float carrier = s->algorithm.carrier_table[s->algorithm.table_index];
            for (int32_t c = 0; c < s->channels; ++c) {
                size_t i = (size_t)n * s->channels + c;
                io->out.samples[i] = io->in.samples[i] * carrier;
            }
            if (++s->algorithm.table_index == (unsigned)s->table_length) s->algorithm.table_index = 0;
        }
    } else {
        float depth = s->depth < 0.0f ? 0.0f : s->depth > 1.0f ? 1.0f : s->depth;
        for (size_t i = 0; i < count; ++i) {
            float mod = 1.0f + depth * sinf(s->algorithm.phase);
            if (mod < 0.0f) mod = 0.0f;
            io->out.samples[i] = io->in.samples[i] * mod;
            s->algorithm.phase += s->algorithm.phase_inc;
            if (s->algorithm.phase >= TWO_PI) s->algorithm.phase -= TWO_PI;
        }
    }
    return orpheus_builtin_sine_mod_emit_out(ctx, &io->out);
}
