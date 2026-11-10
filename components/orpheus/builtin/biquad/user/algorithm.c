#include "component_state.h"
#include <math.h>
#include <string.h>

static void compute_coeffs(const char* type, float fc, float q, float gain_db, float sample_rate,
                           float* b0, float* b1, float* b2, float* a1, float* a2) {
    float A = powf(10.0f, gain_db / 40.0f);
    float w0 = 2.0f * 3.14159265358979f * fc / sample_rate;
    float cosw0 = cosf(w0);
    float sinw0 = sinf(w0);
    float alpha = sinw0 / (2.0f * q);
    *b0 = *b1 = *b2 = *a1 = *a2 = 0.0f;
    if (strcmp(type, "lowpass") == 0) {
        *b0 = (1.0f - cosw0) / 2.0f;
        *b1 = 1.0f - cosw0;
        *b2 = (1.0f - cosw0) / 2.0f;
        float a0 = 1.0f + alpha;
        *a1 = -2.0f * cosw0 / a0;
        *a2 = (1.0f - alpha) / a0;
        *b0 /= a0; *b1 /= a0; *b2 /= a0;
    } else if (strcmp(type, "highpass") == 0) {
        *b0 = (1.0f + cosw0) / 2.0f;
        *b1 = -(1.0f + cosw0);
        *b2 = (1.0f + cosw0) / 2.0f;
        float a0 = 1.0f + alpha;
        *a1 = -2.0f * cosw0 / a0;
        *a2 = (1.0f - alpha) / a0;
        *b0 /= a0; *b1 /= a0; *b2 /= a0;
    } else if (strcmp(type, "bandpass") == 0) {
        *b0 = alpha;
        *b1 = 0.0f;
        *b2 = -alpha;
        float a0 = 1.0f + alpha;
        *a1 = -2.0f * cosw0 / a0;
        *a2 = (1.0f - alpha) / a0;
        *b0 /= a0; *b1 /= a0; *b2 /= a0;
    } else if (strcmp(type, "notch") == 0) {
        *b0 = 1.0f;
        *b1 = -2.0f * cosw0;
        *b2 = 1.0f;
        float a0 = 1.0f + alpha;
        *a1 = -2.0f * cosw0 / a0;
        *a2 = (1.0f - alpha) / a0;
        *b0 /= a0; *b1 /= a0; *b2 /= a0;
    } else if (strcmp(type, "peaking") == 0) {
        *b0 = 1.0f + alpha * A;
        *b1 = -2.0f * cosw0;
        *b2 = 1.0f - alpha * A;
        float a0 = 1.0f + alpha / A;
        *a1 = -2.0f * cosw0 / a0;
        *a2 = (1.0f - alpha / A) / a0;
        *b0 /= a0; *b1 /= a0; *b2 /= a0;
    } else if (strcmp(type, "lowshelf") == 0) {
        float sqrtA = sqrtf(A);
        *b0 = A * ((A + 1.0f) - (A - 1.0f) * cosw0 + 2.0f * sqrtA * alpha);
        *b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw0);
        *b2 = A * ((A + 1.0f) - (A - 1.0f) * cosw0 - 2.0f * sqrtA * alpha);
        float a0 = (A + 1.0f) + (A - 1.0f) * cosw0 + 2.0f * sqrtA * alpha;
        *a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosw0) / a0;
        *a2 = ((A + 1.0f) + (A - 1.0f) * cosw0 - 2.0f * sqrtA * alpha) / a0;
        *b0 /= a0; *b1 /= a0; *b2 /= a0;
    } else if (strcmp(type, "highshelf") == 0) {
        float sqrtA = sqrtf(A);
        *b0 = A * ((A + 1.0f) + (A - 1.0f) * cosw0 + 2.0f * sqrtA * alpha);
        *b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw0);
        *b2 = A * ((A + 1.0f) + (A - 1.0f) * cosw0 - 2.0f * sqrtA * alpha);
        float a0 = (A + 1.0f) - (A - 1.0f) * cosw0 + 2.0f * sqrtA * alpha;
        *a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cosw0) / a0;
        *a2 = ((A + 1.0f) - (A - 1.0f) * cosw0 - 2.0f * sqrtA * alpha) / a0;
        *b0 /= a0; *b1 /= a0; *b2 /= a0;
    }
}

int orpheus_builtin_biquad_reset(orpheus_builtin_biquad_State* s) {
    memset(s->algorithm.z1, 0, sizeof(s->algorithm.z1));
    memset(s->algorithm.z2, 0, sizeof(s->algorithm.z2));
    memset(s->algorithm.x1, 0, sizeof(s->algorithm.x1));
    memset(s->algorithm.x2, 0, sizeof(s->algorithm.x2));
    memset(s->algorithm.y1, 0, sizeof(s->algorithm.y1));
    memset(s->algorithm.y2, 0, sizeof(s->algorithm.y2));
    return ORPHEUS_OK;
}

int orpheus_builtin_biquad_prepare(orpheus_builtin_biquad_State* s, const OrpheusConfig* config) {
    if (config->sample_rate == 0) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(s->form, "df1") != 0 && strcmp(s->form, "df2t") != 0) return ORPHEUS_ERR_INVALID_ARG;
    if (strcmp(s->type, "lowpass") != 0 && strcmp(s->type, "highpass") != 0
        && strcmp(s->type, "bandpass") != 0 && strcmp(s->type, "notch") != 0
        && strcmp(s->type, "peaking") != 0 && strcmp(s->type, "lowshelf") != 0
        && strcmp(s->type, "highshelf") != 0) return ORPHEUS_ERR_INVALID_ARG;
    compute_coeffs(s->type, s->fc, s->q, s->gain_db, (float)config->sample_rate,
        &s->algorithm.b0, &s->algorithm.b1, &s->algorithm.b2, &s->algorithm.a1, &s->algorithm.a2);
    return orpheus_builtin_biquad_reset(s);
}

int orpheus_builtin_biquad_set_parameter(orpheus_builtin_biquad_State* s, const char* key, const OrpheusValue* value) {
    (void)s; (void)key; (void)value;
    return ORPHEUS_ERR_UNSUPPORTED;
}

int orpheus_builtin_biquad_algorithm(orpheus_builtin_biquad_State* s, orpheus_builtin_biquad_IO* io, const OrpheusProcessContext* ctx) {
    if (!io->in.buffer || !io->out.buffer) return ORPHEUS_ERR_INVALID_ARG;
    orpheus_builtin_biquad_AlgorithmState* a = &s->algorithm;
    uint32_t ch = (uint32_t)s->channels;
    for (uint32_t c = 0; c < ch; ++c) {
        for (uint32_t n = 0; n < ctx->frame_count; ++n) {
            float x = io->in.samples[n * ch + c];
            float y;
            if (strcmp(s->form, "df1") == 0) {
                y = a->b0 * x + a->b1 * a->x1[c] + a->b2 * a->x2[c]
                    - a->a1 * a->y1[c] - a->a2 * a->y2[c];
                a->x2[c] = a->x1[c]; a->x1[c] = x;
                a->y2[c] = a->y1[c]; a->y1[c] = y;
            } else {
                y = a->b0 * x + a->z1[c];
                a->z1[c] = a->b1 * x - a->a1 * y + a->z2[c];
                a->z2[c] = a->b2 * x - a->a2 * y;
            }
            io->out.samples[n * ch + c] = y;
        }
    }
    io->out.buffer->frame_count = ctx->frame_count;
    return orpheus_builtin_biquad_emit_out(ctx, &io->out);
}
