#include "component_state.h"
#include "component_api.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int parse_indices(const char* text, int32_t* values, int32_t count, int32_t base) {
    bool supplied = *text != '\0';
    for (int32_t c = 0; c < count; ++c) {
        int32_t value = c + base;
        if (supplied) {
            char* end;
            errno = 0;
            long number = strtol(text, &end, 10);
            if (end == text || errno == ERANGE || number < INT32_MIN || number > INT32_MAX)
                return ORPHEUS_ERR_INVALID_ARG;
            while (isspace((unsigned char)*end)) ++end;
            if (c + 1 < count ? *end != ',' : *end != '\0') return ORPHEUS_ERR_INVALID_ARG;
            value = (int32_t)number;
            text = c + 1 < count ? end + 1 : end;
        }
        values[c] = value;
    }
    return ORPHEUS_OK;
}

static int parse_gains(const char* text, float* values, int32_t count) {
    bool supplied = *text != '\0';
    for (int32_t c = 0; c < count; ++c) {
        float value = 1.0f;
        if (supplied) {
            char* end;
            value = strtof(text, &end);
            if (end == text || !isfinite(value)) return ORPHEUS_ERR_INVALID_ARG;
            while (isspace((unsigned char)*end)) ++end;
            if (c + 1 < count ? *end != ',' : *end != '\0') return ORPHEUS_ERR_INVALID_ARG;
            text = c + 1 < count ? end + 1 : end;
        }
        values[c] = value;
    }
    return ORPHEUS_OK;
}

int orpheus_builtin_channel_selector_validate(const orpheus_builtin_channel_selector_Parameters* p) {
    int32_t indices[64];
    float gains[64];
    int rc = parse_indices(p->indices, indices, p->channels_out, p->index_base);
    return rc ? rc : parse_gains(p->gains, gains, p->channels_out);
}

int orpheus_builtin_channel_selector_prepare(orpheus_builtin_channel_selector_State* s,
    const OrpheusConfig* config) {
    (void)config;
    int rc = parse_indices(s->indices, s->algorithm.indices, s->channels_out, s->index_base);
    return rc ? rc : parse_gains(s->gains, s->algorithm.gains, s->channels_out);
}

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x >= y ? x - y < nb : y - x < na;
}

int orpheus_builtin_channel_selector_reset(orpheus_builtin_channel_selector_State* s) {
    (void)s;
    return ORPHEUS_OK;
}

int orpheus_builtin_channel_selector_algorithm(orpheus_builtin_channel_selector_State* s,
    orpheus_builtin_channel_selector_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    const OrpheusBuffer* inputs[] = {io->a.buffer, io->b.buffer, io->base.buffer};
    for (unsigned i = 0; i < 3; ++i)
        if (inputs[i] && (!inputs[i]->data || inputs[i]->frame_capacity < ctx->frame_count))
            return ORPHEUS_ERR_INVALID_ARG;
    if (!io->out.buffer) return ORPHEUS_OK;
    size_t bytes = (size_t)ctx->frame_count * s->channels_out * sizeof(float);
    for (unsigned i = 0; i < 3; ++i)
        if (inputs[i] && overlap(inputs[i]->data, (size_t)ctx->frame_count * inputs[i]->channels * sizeof(float),
            io->out.samples, bytes)) return ORPHEUS_ERR_INVALID_ARG;
    int64_t total = (int64_t)s->channels_a + s->channels_b;
    for (int32_t c = 0; c < s->channels_out; ++c) {
        int64_t index = (int64_t)s->algorithm.indices[c] - s->index_base;
        if (index >= total && s->high_index_policy) index = total - 1;
        const OrpheusBuffer* input = !s->enabled || index < 0 || index >= total
            ? NULL : index < s->channels_a ? io->a.buffer : io->b.buffer;
        uint32_t channel = index < s->channels_a ? (uint32_t)index : (uint32_t)(index - s->channels_a);
        for (uint32_t n = 0; n < ctx->frame_count; ++n) {
            float* output = io->out.samples + (size_t)n * s->channels_out + c;
            const float* original = s->accumulate && io->base.buffer
                ? io->base.samples + (size_t)n * s->channels_out + c : NULL;
            if (!input) {
                if (original) memcpy(output, original, sizeof(float));
                else *output = 0.0f;
            } else {
                const float* sample = (const float*)input->data + (size_t)n * input->channels + channel;
                if (s->accumulate) {
                    volatile float contribution = s->apply_gain ? *sample * s->algorithm.gains[c] : *sample;
                    *output = (original ? *original : 0.0f) + contribution;
                } else if (s->apply_gain) *output = *sample * s->algorithm.gains[c];
                else memcpy(output, sample, sizeof(float));
            }
        }
    }
    return orpheus_builtin_channel_selector_emit_out(ctx, &io->out);
}
