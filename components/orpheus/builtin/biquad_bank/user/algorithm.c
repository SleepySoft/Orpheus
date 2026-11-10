#include "component_state.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static void peaking_coefficients(float* coefficients, float fc, float q, float gain_db, float sample_rate) {
    float A = powf(10.0f, gain_db / 40.0f);
    float w0 = 2.0f * 3.14159265358979f * fc / sample_rate;
    float cosw0 = cosf(w0);
    float alpha = sinf(w0) / (2.0f * q);
    float a0 = 1.0f + alpha / A;
    coefficients[0] = (1.0f + alpha * A) / a0;
    coefficients[1] = -2.0f * cosw0 / a0;
    coefficients[2] = (1.0f - alpha * A) / a0;
    coefficients[3] = -2.0f * cosw0 / a0;
    coefficients[4] = (1.0f - alpha / A) / a0;
}

int orpheus_builtin_biquad_bank_reset(orpheus_builtin_biquad_bank_State* s) {
    memset(&s->algorithm.stage0.history, 0, sizeof(s->algorithm.stage0.history));
    memset(&s->algorithm.stage1.history, 0, sizeof(s->algorithm.stage1.history));
    return ORPHEUS_OK;
}

static int initialize_coefficients(float* coefficients, const char* initial, float fc, float q,
        float gain_db, float sample_rate) {
    if (!initial[0]) {
        peaking_coefficients(coefficients, fc, q, gain_db, sample_rate);
        return ORPHEUS_OK;
    }
    for (uint32_t i = 0; i < 5; ++i) {
        char* end;
        float number = strtof(initial, &end);
        if (end == initial || !isfinite(number)) return ORPHEUS_ERR_INVALID_ARG;
        coefficients[i] = number;
        if (i < 4) {
            if (*end != ',') return ORPHEUS_ERR_INVALID_ARG;
            initial = end + 1;
        } else if (*end) {
            return ORPHEUS_ERR_INVALID_ARG;
        }
    }
    return ORPHEUS_OK;
}

int orpheus_builtin_biquad_bank_prepare(orpheus_builtin_biquad_bank_State* s, const OrpheusConfig* config) {
    if (config->sample_rate <= 0.0 ||
        (strcmp(s->form, "df1") != 0 && strcmp(s->form, "df2t") != 0)) return ORPHEUS_ERR_INVALID_ARG;
    int rc = initialize_coefficients(s->algorithm.stage0.coefs, s->bq0_coefs,
        s->fc0, s->q0, s->gain_db0, (float)config->sample_rate);
    if (rc != ORPHEUS_OK) return rc;
    rc = initialize_coefficients(s->algorithm.stage1.coefs, s->bq1_coefs,
        s->fc1, s->q1, s->gain_db1, (float)config->sample_rate);
    if (rc != ORPHEUS_OK) return rc;
    return orpheus_builtin_biquad_bank_reset(s);
}

int orpheus_builtin_biquad_bank_set_parameter(orpheus_builtin_biquad_bank_State* s,
        const char* key, const OrpheusValue* value) {
    (void)s; (void)key; (void)value;
    return ORPHEUS_ERR_UNSUPPORTED;
}

static float tick(orpheus_builtin_biquad_bank_History* stage, const float* coefficients,
        bool df1, uint32_t c, float x) {
    if (df1) {
        float y = coefficients[0] * x + coefficients[1] * stage->x1[c] + coefficients[2] * stage->x2[c]
            - coefficients[3] * stage->y1[c] - coefficients[4] * stage->y2[c];
        stage->x2[c] = stage->x1[c]; stage->x1[c] = x;
        stage->y2[c] = stage->y1[c]; stage->y1[c] = y;
        return y;
    }
    float y = coefficients[0] * x + stage->z1[c];
    stage->z1[c] = coefficients[1] * x - coefficients[3] * y + stage->z2[c];
    stage->z2[c] = coefficients[2] * x - coefficients[4] * y;
    return y;
}

int orpheus_builtin_biquad_bank_algorithm(orpheus_builtin_biquad_bank_State* s,
        orpheus_builtin_biquad_bank_IO* io, const OrpheusProcessContext* ctx) {
    if (!io->in.buffer || !io->out.buffer) return ORPHEUS_ERR_INVALID_ARG;
    bool df1 = strcmp(s->form, "df1") == 0;
    uint32_t channels = (uint32_t)s->channels;
    for (uint32_t c = 0; c < channels; ++c) {
        for (uint32_t n = 0; n < ctx->frame_count; ++n) {
            float x = io->in.samples[n * channels + c];
            x = tick(&s->algorithm.stage0.history, s->algorithm.stage0.coefs, df1, c, x);
            io->out.samples[n * channels + c] = tick(&s->algorithm.stage1.history, s->algorithm.stage1.coefs, df1, c, x);
        }
    }
    io->out.buffer->frame_count = ctx->frame_count;
    return orpheus_builtin_biquad_bank_emit_out(ctx, &io->out);
}
