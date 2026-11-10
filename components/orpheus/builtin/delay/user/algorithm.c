#include "component_state.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

int orpheus_builtin_delay_prepare(orpheus_builtin_delay_State* s, const OrpheusConfig* config) {
    double samples = (s->delay_ms / 1000.0f) * config->sample_rate + 0.5f;
    if (!isfinite(samples) || samples < 0 || samples > UINT32_MAX) return ORPHEUS_ERR_OUT_OF_MEMORY;
    uint32_t delay_samples = (uint32_t)samples;
    uint32_t block_size = config->block_size > 0 ? config->block_size : 1024;
    uint64_t capacity = ((uint64_t)delay_samples + block_size + 1) * (uint32_t)s->channels;
    if (capacity > UINT32_MAX || capacity > SIZE_MAX / sizeof(float)) return ORPHEUS_ERR_OUT_OF_MEMORY;
    float* buffer = (float*)calloc((size_t)capacity, sizeof(float));
    if (!buffer) return ORPHEUS_ERR_OUT_OF_MEMORY;
    free(s->algorithm.buffer);
    s->algorithm.buffer = buffer;
    s->algorithm.capacity = (uint32_t)capacity;
    s->algorithm.delay_samples = delay_samples;
    s->algorithm.write_pos = 0;
    s->algorithm.mix_smoothed = s->mix;
    s->algorithm.mix_coeff = 1.0f;
    if (config->sample_rate > 0) {
        s->algorithm.mix_coeff = 1.0f - expf(-1.0f / (0.005f * (float)config->sample_rate));
        if (s->algorithm.mix_coeff > 1.0f) s->algorithm.mix_coeff = 1.0f;
    }
    return ORPHEUS_OK;
}

int orpheus_builtin_delay_set_parameter(orpheus_builtin_delay_State* s, const char* key, const OrpheusValue* value) {
    (void)s; (void)value;
    return strcmp(key, "mix") == 0 ? ORPHEUS_OK : ORPHEUS_ERR_UNSUPPORTED;
}

int orpheus_builtin_delay_destroy(orpheus_builtin_delay_State* s) {
    free(s->algorithm.buffer);
    s->algorithm.buffer = NULL;
    s->algorithm.capacity = 0;
    return ORPHEUS_OK;
}

int orpheus_builtin_delay_reset(orpheus_builtin_delay_State* s) {
    if (s->algorithm.buffer) memset(s->algorithm.buffer, 0, s->algorithm.capacity * sizeof(float));
    s->algorithm.write_pos = 0;
    if (s->algorithm.mix_coeff > 0.0f) s->algorithm.mix_smoothed = s->mix;
    return ORPHEUS_OK;
}

int orpheus_builtin_delay_algorithm(orpheus_builtin_delay_State* s, orpheus_builtin_delay_IO* io, const OrpheusProcessContext* ctx) {
    if (!io->in.buffer || !io->out.buffer || !s->algorithm.buffer) return ORPHEUS_ERR_INVALID_ARG;
    uint32_t ch = (uint32_t)s->channels;
    for (uint32_t n = 0; n < ctx->frame_count; ++n) {
        for (uint32_t c = 0; c < ch; ++c) {
            float x = io->in.samples[n * ch + c];
            /* 零延迟先写后读，保留逐样本、逐通道的混合平滑顺序。 */
            s->algorithm.buffer[s->algorithm.write_pos + c] = x;
            uint32_t idx = (s->algorithm.write_pos + s->algorithm.capacity - s->algorithm.delay_samples * ch) % s->algorithm.capacity;
            float delayed = s->algorithm.buffer[idx + c];
            s->algorithm.mix_smoothed += s->algorithm.mix_coeff * (s->mix - s->algorithm.mix_smoothed);
            float m = s->algorithm.mix_smoothed;
            io->out.samples[n * ch + c] = (1.0f - m) * x + m * delayed;
        }
        s->algorithm.write_pos = (s->algorithm.write_pos + ch) % s->algorithm.capacity;
    }
    io->out.buffer->frame_count = ctx->frame_count;
    return orpheus_builtin_delay_emit_out(ctx, &io->out);
}
