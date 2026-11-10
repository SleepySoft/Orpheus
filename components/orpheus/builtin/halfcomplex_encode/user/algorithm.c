#include "component_state.h"
#include "component_api.h"
#include <string.h>

int orpheus_builtin_halfcomplex_encode_validate(const orpheus_builtin_halfcomplex_encode_Parameters* s) {
    return s->fft_size % 2 || s->bins != s->fft_size / 2 + 1 ? ORPHEUS_ERR_INVALID_ARG : ORPHEUS_OK;
}

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x >= y ? x - y < nb : y - x < na;
}

int orpheus_builtin_halfcomplex_encode_algorithm(orpheus_builtin_halfcomplex_encode_State* s,
    orpheus_builtin_halfcomplex_encode_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    if (!io->in.buffer) return ORPHEUS_ERR_INVALID_ARG;
    size_t input_bytes = (size_t)s->bins * 2 * s->batches * s->channels * sizeof(float);
    size_t output_bytes = (size_t)s->fft_size * s->batches * s->channels * sizeof(float);
    OrpheusBuffer* outputs[] = {io->out.buffer, io->spectrum.buffer};
    for (unsigned i = 0; i < 2; ++i)
        if (outputs[i] && overlap(io->in.samples, input_bytes, outputs[i]->data, output_bytes))
            return ORPHEUS_ERR_INVALID_ARG;
    if (io->out.buffer && io->spectrum.buffer
        && overlap(io->out.samples, output_bytes, io->spectrum.samples, output_bytes))
        return ORPHEUS_ERR_INVALID_ARG;
    for (unsigned i = 0; i < 2; ++i)
        if (outputs[i]) {
            unsigned char* output = (unsigned char*)outputs[i]->data;
            const unsigned char* input = (const unsigned char*)io->in.samples;
            for (int32_t h = 0; h < s->batches; ++h)
                for (int32_t k = 0; k < s->bins; ++k)
                    for (int32_t c = 0; c < s->channels; ++c) {
                        size_t source = ((size_t)h * s->bins * 2 + k * 2) * s->channels + c;
                        size_t real = ((size_t)h * s->fft_size + k) * s->channels + c;
                        memcpy(output + real * 4, input + source * 4, 4);
                        if (k && k < s->fft_size / 2) {
                            size_t imaginary = ((size_t)h * s->fft_size + s->fft_size - k) * s->channels + c;
                            memcpy(output + imaginary * 4, input + (source + s->channels) * 4, 4);
                        }
                    }
        }
    int rc = orpheus_builtin_halfcomplex_encode_emit_out(ctx, &io->out);
    return rc ? rc : orpheus_builtin_halfcomplex_encode_emit_spectrum(ctx, &io->spectrum);
}
