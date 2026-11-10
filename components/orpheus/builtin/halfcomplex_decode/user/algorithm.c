#include "component_state.h"
#include "component_api.h"
#include <string.h>

int orpheus_builtin_halfcomplex_decode_validate(const orpheus_builtin_halfcomplex_decode_Parameters* p) {
    return p->fft_size % 2 || p->first_bin + p->bins > p->fft_size / 2 + 1
        ? ORPHEUS_ERR_INVALID_ARG : ORPHEUS_OK;
}

static bool overlap(const void* a, size_t na, const void* b, size_t nb) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x >= y ? x - y < nb : y - x < na;
}

int orpheus_builtin_halfcomplex_decode_algorithm(orpheus_builtin_halfcomplex_decode_State* s,
    orpheus_builtin_halfcomplex_decode_IO* io, const OrpheusProcessContext* ctx) {
    if (!ctx->frame_count) return ORPHEUS_OK;
    const OrpheusBuffer* in = io->in.buffer ? io->in.buffer : io->spectrum.buffer;
    if (!in) return ORPHEUS_ERR_INVALID_ARG;
    size_t input_bytes = (size_t)s->fft_size * s->batches * s->channels * sizeof(float);
    size_t output_bytes = (size_t)s->bins * 2 * s->batches * s->channels * sizeof(float);
    if ((io->out.buffer && overlap(in->data, input_bytes, io->out.samples, output_bytes))
        || (io->tick.buffer && overlap(in->data, input_bytes, io->tick.samples, sizeof(int32_t)))
        || (io->out.buffer && io->tick.buffer
            && overlap(io->out.samples, output_bytes, io->tick.samples, sizeof(int32_t))))
        return ORPHEUS_ERR_INVALID_ARG;
    if (io->out.buffer) {
        const unsigned char* input = (const unsigned char*)in->data;
        unsigned char* output = (unsigned char*)io->out.samples;
        const uint32_t zero = 0;
        for (int32_t h = 0; h < s->batches; ++h)
            for (int32_t bin = 0; bin < s->bins; ++bin) {
                int32_t k = s->first_bin + bin;
                for (int32_t c = 0; c < s->channels; ++c) {
                    size_t target = ((size_t)h * s->bins * 2 + bin * 2) * s->channels + c;
                    size_t real = ((size_t)h * s->fft_size + k) * s->channels + c;
                    memcpy(output + target * 4, input + real * 4, 4);
                    if (k == 0 || k == s->fft_size / 2)
                        memcpy(output + (target + s->channels) * 4, &zero, 4);
                    else {
                        size_t imaginary = ((size_t)h * s->fft_size + s->fft_size - k) * s->channels + c;
                        memcpy(output + (target + s->channels) * 4, input + imaginary * 4, 4);
                    }
                }
            }
    }
    if (io->tick.buffer) io->tick.samples[0] = 1;
    int rc = orpheus_builtin_halfcomplex_decode_emit_out(ctx, &io->out);
    return rc ? rc : orpheus_builtin_halfcomplex_decode_emit_tick(ctx, &io->tick);
}
