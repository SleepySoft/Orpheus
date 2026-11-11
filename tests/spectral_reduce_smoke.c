#include "component_state.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); return 1; } } while (0)
typedef struct Capture { OrpheusBuffer* output; unsigned calls; int error; } Capture;
static int emit(void* user, uint32_t port, const OrpheusDelivery* delivery) {
    Capture* capture = user;
    if (port != 1 || !delivery || delivery->buffer != capture->output) return ORPHEUS_ERR_INVALID_ARG;
    capture->calls++;
    return capture->error;
}

static float expected(const float* data, unsigned channel, unsigned channels, unsigned frames,
    unsigned fft, unsigned batches, unsigned bins, unsigned op) {
    float total = 0, least = FLT_MAX, most = -FLT_MAX;
    unsigned count = 0;
    for (unsigned frame = 0; frame < frames; frame++) {
        if (frame / fft >= batches || frame % fft >= bins) continue;
        float sample = data[(size_t)frame * channels + channel];
        total += sample;
        if (sample < least) least = sample;
        if (sample > most) most = sample;
        count++;
    }
    return !count ? 0 : op == 0 ? total : op == 1 ? total / (float)count : op == 2 ? least : most;
}

static int shape(const OrpheusComponentInterface* api, unsigned channels, unsigned frames,
    unsigned fft, unsigned batches, unsigned requested_bins, bool special) {
    size_t count = (size_t)frames * channels;
    float* input = malloc((count + 1) * sizeof(float));
    float* output = malloc(count * sizeof(float));
    CHECK(input && output);
    for (unsigned f = 0; f < frames; f++) for (unsigned c = 0; c < channels; c++)
        input[(size_t)f * channels + c] = ((int)((f * 17 + c * 31) % 103) - 51) * .03125f;
    if (special) for (unsigned f = 0; f < frames; f++) {
        const uint32_t bits[] = {0x7fc12345, 0x7f800000, 0xff800000, 0x80000000,
            0, 0x7f7fffff, 0xff7fffff, 1};
        for (unsigned c = 0; c < channels; c++)
            memcpy(input + (size_t)f * channels + c, &bits[c % 8], sizeof(float));
    }
    const char* operations[] = {"sum", "mean", "min", "max"};
    const char* ids[] = {"channels", "fft_size", "num_frames", "bin_count", "operation"};
    OrpheusValue params[] = {
        {.type=ORPHEUS_VALUE_INT, .value.i32=channels},
        {.type=ORPHEUS_VALUE_INT, .value.i32=fft},
        {.type=ORPHEUS_VALUE_INT, .value.i32=batches},
        {.type=ORPHEUS_VALUE_INT, .value.i32=requested_bins},
        {.type=ORPHEUS_VALUE_STRING, .value.str="mean"},
    };
    orpheus_builtin_spectral_reduce_State state;
    void* handle = NULL;
    OrpheusConfig config = {.sample_rate=48000, .block_size=frames, .state_block=&state,
        .param_ids=ids, .param_values=params, .param_count=5};
    CHECK(api->create(&handle, &config) == 0 && handle == &state);
    OrpheusBuffer in = {.data=input, .channels=channels, .frame_count=frames, .frame_capacity=frames,
        .format=ORPHEUS_FORMAT_F32, .interleaved=true};
    OrpheusBuffer out = in; out.data=output; out.frame_count=0;
    const OrpheusBuffer* inputs[] = {&in}; OrpheusBuffer* outputs[] = {&out};
    Capture capture = {.output=&out};
    OrpheusContinuation continuation = {.emit=emit, .user=&capture};
    OrpheusProcessContext ctx = {.inputs=inputs, .outputs=outputs, .input_count=1, .output_count=1,
        .frame_count=frames, .continuation=&continuation};
    unsigned bins = requested_bins && requested_bins <= fft ? requested_bins : fft / 2 + 1;
    for (unsigned op = 0; op < 4; op++) {
        params[4].value.str=operations[op];
        CHECK(api->prepare(handle, &config) == 0);
        for (unsigned pass = 0; pass < 3; pass++) {
            unsigned actual = pass == 1 ? 1 : pass == 2 ? frames - 1 : frames;
            in.frame_count=ctx.frame_count=actual; capture.calls=0;
            CHECK(api->process(handle, &ctx) == 0 && capture.calls == (actual ? 1u : 0u));
            if (actual) CHECK(out.frame_count == actual);
            for (unsigned c = 0; c < channels; c++) {
                float want = expected(input, c, channels, actual, fft, batches, bins, op);
                for (unsigned f = 0; f < actual; f++) {
                    float got = output[(size_t)f * channels + c];
                    CHECK(isnan(want) ? isnan(got) : !memcmp(&want, &got, sizeof(float)));
                }
            }
        }
    }
    in.frame_count=ctx.frame_count=frames;
    capture.error=-77; capture.calls=0;
    CHECK(api->process(handle, &ctx) == -77 && capture.calls == 1);
    capture.error=0; capture.calls=0;
    memset(output, 0x5a, count * sizeof(float));
    out.frame_capacity=frames-1;
    CHECK(api->process(handle, &ctx) == ORPHEUS_ERR_INVALID_ARG && !capture.calls);
    out.frame_capacity=frames;
    for (size_t i = 0; i < count * sizeof(float); i++) CHECK(((unsigned char*)output)[i] == 0x5a);
    in.frame_count=frames-1;
    CHECK(api->process(handle, &ctx) == ORPHEUS_ERR_INVALID_ARG && !capture.calls);
    in.frame_count=frames;
    outputs[0]=NULL; CHECK(api->process(handle, &ctx) == ORPHEUS_ERR_INVALID_ARG);
    outputs[0]=&out; inputs[0]=NULL;
    CHECK(api->process(handle, &ctx) == ORPHEUS_ERR_INVALID_ARG); inputs[0]=&in;
    out.data=input; CHECK(api->process(handle, &ctx) == ORPHEUS_ERR_INVALID_ARG);
    if (count > 1) {
        out.data=input+1; CHECK(api->process(handle, &ctx) == ORPHEUS_ERR_INVALID_ARG);
    }
    out.data=output;
    orpheus_builtin_spectral_reduce_State saved=state;
    params[4].value.str="oops";
    CHECK(api->prepare(handle, &config) == ORPHEUS_ERR_INVALID_ARG && !memcmp(&state, &saved, sizeof(state)));
    params[4].value.str="mean"; params[0].value.i32=65;
    CHECK(api->prepare(handle, &config) == ORPHEUS_ERR_INVALID_ARG && !memcmp(&state, &saved, sizeof(state)));
    params[0].value.i32=channels; params[1].value.i32=4097;
    CHECK(api->prepare(handle, &config) == ORPHEUS_ERR_INVALID_ARG);
    params[1].value.i32=fft; params[2].value.i32=257;
    CHECK(api->prepare(handle, &config) == ORPHEUS_ERR_INVALID_ARG);
    params[2].value.i32=batches; params[3].value.i32=4097;
    CHECK(api->prepare(handle, &config) == ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->reset(handle) == 0 && api->destroy(handle) == 0);
    config.state_block=NULL; CHECK(api->create(&handle, &config) == ORPHEUS_ERR_INVALID_ARG);
    free(input); free(output);
    return 0;
}

int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    CHECK(shape(api, 1, 1, 2, 1, 0, false) == 0);
    CHECK(shape(api, 3, 17, 4, 3, 2, false) == 0);
    CHECK(shape(api, 64, 4096, 128, 12, 0, false) == 0);
    CHECK(shape(api, 2, 8192, 4096, 2, 4096, false) == 0);
    CHECK(shape(api, 3, 1024, 4, 256, 4096, false) == 0);
    CHECK(shape(api, 8, 8, 2, 4, 2, true) == 0);
    return 0;
}
