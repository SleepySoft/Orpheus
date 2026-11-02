#include "orpheus_rational_resample.h"
#include "orpheus_block_split.h"
#include "orpheus_circular_buffer.h"
#include "orpheus_resample.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

const OrpheusComponentInterface* test_rational_resample(void);
const OrpheusComponentInterface* test_block_split(void);
const OrpheusComponentInterface* test_circular_buffer(void);
const OrpheusComponentInterface* test_resample(void);

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "音频适配断言失败：%s:%d: %s\n", __FILE__, __LINE__, #condition); \
    return 1; } } while (0)

static OrpheusValue integer(int value) {
    OrpheusValue result = {.type=ORPHEUS_VALUE_INT, .value.i32=value};
    return result;
}

static int test_src(uint32_t up, uint32_t down, uint32_t frames, double rate) {
    const OrpheusComponentInterface* api = test_rational_resample();
    RationalResampleState state = {0};
    const char* ids[] = {"up", "down", "taps"};
    OrpheusValue values[] = {integer((int)up), integer((int)down), integer(128)};
    OrpheusConfig cfg = {.sample_rate=rate, .block_size=frames, .channels=2,
        .param_ids=ids, .param_values=values, .param_count=3, .state_block=&state};
    void* handle = NULL;
    CHECK(api->create(&handle, &cfg) == ORPHEUS_OK && handle == &state);
    CHECK(api->prepare(handle, &cfg) == ORPHEUS_OK);
    float input[2048], output[2048], first[2048];
    const uint32_t produced = frames * up / down;
    CHECK(frames <= 1024 && produced <= 1024);
    OrpheusBuffer in = {.data=input, .format=ORPHEUS_FORMAT_F32, .channels=2,
        .frame_capacity=frames, .frame_count=frames, .interleaved=true};
    OrpheusBuffer out = {.data=output, .format=ORPHEUS_FORMAT_F32, .channels=2,
        .frame_capacity=produced, .interleaved=true};
    const OrpheusBuffer* inputs[] = {&in};
    OrpheusBuffer* outputs[] = {&out};
    OrpheusProcessContext ctx = {.inputs=inputs, .outputs=outputs,
        .input_count=1, .output_count=1, .frame_count=frames, .sample_rate=rate};
    const double pi = 3.14159265358979323846;
    double error = 0.0, energy = 0.0;
    for (uint32_t block = 0; block < 16; ++block) {
        for (uint32_t f = 0; f < frames; ++f) {
            input[2*f] = (float)sin(2.0*pi*1000.0*(block*frames+f)/rate);
            input[2*f+1] = 0.0f;
        }
        CHECK(api->process(handle, &ctx) == ORPHEUS_OK);
        CHECK(out.frame_count == produced);
        if (block == 0) memcpy(first, output, produced*2*sizeof(float));
        for (uint32_t f = 0; f < produced; ++f) {
            CHECK(output[2*f+1] == 0.0f);
            if (block > 2) {
                const double position = (double)(block*produced+f)*down/up -
                    (up == down ? 0.0 : 63.5);
                const double expected = sin(2.0*pi*1000.0*position/rate);
                error += (output[2*f]-expected)*(output[2*f]-expected);
                energy += expected*expected;
            }
        }
    }
    CHECK(sqrt(error/energy) < 0.005);
    CHECK(api->reset(handle) == ORPHEUS_OK);
    for (uint32_t f = 0; f < frames; ++f) {
        input[2*f] = (float)sin(2.0*pi*1000.0*f/rate);
        input[2*f+1] = 0.0f;
    }
    CHECK(api->process(handle, &ctx) == ORPHEUS_OK);
    CHECK(memcmp(first, output, produced*2*sizeof(float)) == 0);
    if (frames % 2 == 0 && ((frames/2)*up) % down == 0) {
        CHECK(api->reset(handle) == ORPHEUS_OK);
        ctx.frame_count = frames/2;
        const uint32_t half_output = produced/2;
        CHECK(api->process(handle, &ctx) == ORPHEUS_OK);
        in.data = input + frames;
        out.data = output + half_output*2;
        CHECK(api->process(handle, &ctx) == ORPHEUS_OK);
        CHECK(memcmp(first, output, produced*2*sizeof(float)) == 0);
        in.data = input;
        out.data = output;
        ctx.frame_count = frames;
    }

    CHECK(api->reset(handle) == ORPHEUS_OK);
    for (uint32_t f = 0; f < frames*2; ++f) input[f] = 1.0f;
    for (uint32_t block = 0; block < 8; ++block)
        CHECK(api->process(handle, &ctx) == ORPHEUS_OK);
    for (uint32_t f = 0; f < produced*2; ++f) CHECK(fabs(output[f]-1.0f) < 1e-5);

    if (up == 3 && down == 13) {
        CHECK(api->reset(handle) == ORPHEUS_OK);
        energy = 0.0;
        for (uint32_t block = 0; block < 16; ++block) {
            for (uint32_t f = 0; f < frames; ++f) {
                input[2*f] = (float)sin(2.0*pi*16000.0*(block*frames+f)/rate);
                input[2*f+1] = 0.0f;
            }
            CHECK(api->process(handle, &ctx) == ORPHEUS_OK);
            if (block > 2)
                for (uint32_t f = 0; f < produced; ++f) energy += output[2*f]*output[2*f];
        }
        CHECK(sqrt(energy/(13*produced)) < 0.001);
    }
    out.frame_capacity = produced - 1;
    CHECK(api->process(handle, &ctx) == ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->destroy(handle) == ORPHEUS_OK);
    cfg.block_size = 1;
    if (up % down != 0) CHECK(api->prepare(handle, &cfg) == ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->destroy(handle) == ORPHEUS_OK);
    return 0;
}

static int test_split(void) {
    const OrpheusComponentInterface* api = test_block_split();
    BlockSplitState state = {0};
    const char* ids[] = {"factor"};
    OrpheusValue values[] = {integer(4)};
    OrpheusConfig cfg = {.sample_rate=48000, .block_size=128, .channels=2,
        .param_ids=ids, .param_values=values, .param_count=1, .state_block=&state};
    void* handle = NULL;
    CHECK(api->create(&handle, &cfg) == ORPHEUS_OK);
    CHECK(api->prepare(handle, &cfg) == ORPHEUS_OK);
    float input[256], output[64];
    OrpheusBuffer in = {.data=input, .format=ORPHEUS_FORMAT_F32, .channels=2,
        .frame_capacity=128, .frame_count=128, .interleaved=true};
    OrpheusBuffer out = {.data=output, .format=ORPHEUS_FORMAT_F32, .channels=2,
        .frame_capacity=32, .interleaved=true};
    const OrpheusBuffer* inputs[] = {&in};
    OrpheusBuffer* outputs[] = {&out};
    OrpheusProcessContext ctx = {.inputs=inputs, .outputs=outputs, .input_count=1,
        .output_count=1, .frame_count=32, .sample_rate=48000};
    for (uint32_t round = 0; round < 2; ++round) {
        for (uint32_t call = 0; call < 16; ++call) {
            for (uint32_t f = 0; f < 128; ++f) {
                input[2*f] = (float)((call/4)*128 + f + 1);
                input[2*f+1] = -input[2*f];
            }
            CHECK(api->process(handle, &ctx) == ORPHEUS_OK);
            for (uint32_t f = 0; f < 32; ++f) {
                const float expected = call < 3 ? 0.0f : (float)((call-3)*32+f+1);
                CHECK(output[2*f] == expected && output[2*f+1] == -expected);
            }
        }
        CHECK(api->reset(handle) == ORPHEUS_OK);
    }
    CHECK(api->destroy(handle) == ORPHEUS_OK);
    cfg.block_size = 127;
    CHECK(api->prepare(handle, &cfg) == ORPHEUS_ERR_INVALID_ARG);
    return 0;
}

static int test_window(void) {
    const OrpheusComponentInterface* api = test_circular_buffer();
    CircularBufferState state = {0};
    const char* ids[] = {"frame_size", "hop_size", "num_frames"};
    OrpheusValue values[] = {integer(256), integer(128), integer(1)};
    OrpheusConfig cfg = {.sample_rate=3000, .block_size=128, .channels=2,
        .param_ids=ids, .param_values=values, .param_count=3, .state_block=&state};
    void* handle = NULL;
    CHECK(api->create(&handle, &cfg) == ORPHEUS_OK);
    CHECK(api->prepare(handle, &cfg) == ORPHEUS_OK);
    float input[512], output[1024];
    OrpheusBuffer in = {.data=input, .format=ORPHEUS_FORMAT_F32, .channels=2,
        .frame_capacity=128, .frame_count=128, .interleaved=true};
    OrpheusBuffer out = {.data=output, .format=ORPHEUS_FORMAT_F32, .channels=2,
        .frame_capacity=256, .interleaved=true};
    const OrpheusBuffer* inputs[] = {&in};
    OrpheusBuffer* outputs[] = {&out};
    OrpheusProcessContext ctx = {.inputs=inputs, .outputs=outputs, .input_count=1,
        .output_count=1, .frame_count=128, .sample_rate=3000};
    for (uint32_t round = 0; round < 2; ++round) {
        for (uint32_t block = 0; block < 3; ++block) {
            for (uint32_t f = 0; f < 128; ++f) {
                input[2*f] = (float)(block*128+f+1);
                input[2*f+1] = -input[2*f];
            }
            CHECK(api->process(handle, &ctx) == ORPHEUS_OK && out.frame_count == 256);
            for (uint32_t f = 0; f < 256; ++f) {
                const float expected = block == 0 && f < 128 ? 0.0f :
                    (float)((int32_t)block*128 + (int32_t)f - 127);
                CHECK(output[2*f] == expected && output[2*f+1] == -expected);
            }
        }
        CHECK(api->reset(handle) == ORPHEUS_OK);
    }
    out.frame_capacity = 128;
    CHECK(api->process(handle, &ctx) == ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->destroy(handle) == ORPHEUS_OK);
    values[2] = integer(2);
    cfg.block_size = 256;
    in.frame_capacity = in.frame_count = 256;
    out.frame_capacity = 512;
    ctx.frame_count = 256;
    CHECK(api->prepare(handle, &cfg) == ORPHEUS_OK);
    for (uint32_t f = 0; f < 256; ++f) {
        input[2*f] = (float)(f+1);
        input[2*f+1] = -input[2*f];
    }
    CHECK(api->process(handle, &ctx) == ORPHEUS_OK && out.frame_count == 512);
    for (uint32_t f = 0; f < 512; ++f) {
        const float expected = f < 128 ? 0.0f : (float)(f < 256 ? f-127 : f-255);
        CHECK(output[2*f] == expected && output[2*f+1] == -expected);
    }
    CHECK(api->destroy(handle) == ORPHEUS_OK);
    cfg.block_size = 127;
    CHECK(api->prepare(handle, &cfg) == ORPHEUS_ERR_INVALID_ARG);
    return 0;
}

static int test_integer_decimation(void) {
    const OrpheusComponentInterface* api = test_resample();
    const uint32_t factors[] = {2, 16, 64};
    for (uint32_t variant = 0; variant < 3; ++variant) {
        uint32_t factor = factors[variant];
        for (int mode = 0; mode < 2; ++mode) {
            for (int last = 0; last < 2; ++last) {
                ResampleState state = {0};
                const char* ids[] = {"factor", "mode", "phase"};
                uint32_t phase = last ? factor - 1 : 0;
                OrpheusValue values[] = {integer((int)factor), integer(mode), integer((int)phase)};
                OrpheusConfig cfg = {.channels=2, .sample_rate=48000, .block_size=32,
                    .param_ids=ids, .param_values=values, .param_count=3};
                CHECK(api->prepare(&state, &cfg) == ORPHEUS_OK);
                for (int pass = 0; pass < 2; ++pass) {
                    if (pass) CHECK(api->reset(&state) == ORPHEUS_OK);
                    float input[64], output[16];
                    OrpheusBuffer in = {.data=input, .format=ORPHEUS_FORMAT_F32,
                        .channels=2, .frame_capacity=32, .interleaved=true};
                    OrpheusBuffer out = {.data=output, .format=ORPHEUS_FORMAT_F32,
                        .channels=2, .frame_capacity=8, .interleaved=true};
                    const OrpheusBuffer* inputs[] = {&in};
                    OrpheusBuffer* outputs[] = {&out};
                    OrpheusProcessContext ctx = {.inputs=inputs, .outputs=outputs,
                        .input_count=1, .output_count=1};
                    uint32_t consumed = 0;
                    while (consumed < factor * 8) {
                        uint32_t count = (consumed % 2) ? 7 : 5;
                        if (count > factor * 8 - consumed) count = factor * 8 - consumed;
                        for (uint32_t i = 0; i < count; ++i) {
                            input[2*i] = (float)(consumed + i + 1);
                            input[2*i+1] = -input[2*i];
                        }
                        ctx.frame_count = in.frame_count = count;
                        CHECK(api->process(&state, &ctx) == ORPHEUS_OK);
                        consumed += count;
                    }
                    CHECK(out.frame_count == 8);
                    for (uint32_t i = 0; i < 8; ++i) {
                        float expected = mode ? (float)(i*factor + phase + 1) :
                            (float)(i*factor) + ((float)factor + 1.0f)*0.5f;
                        CHECK(output[2*i] == expected && output[2*i+1] == -expected);
                    }
                }
                OrpheusValue value;
                CHECK(api->get_parameter(&state, "mode", &value) == ORPHEUS_OK);
                CHECK(value.value.i32 == mode);
                CHECK(api->get_parameter(&state, "phase", &value) == ORPHEUS_OK);
                CHECK(value.value.i32 == (int)phase);
                values[2] = integer((int)factor);
                CHECK(api->prepare(&state, &cfg) == ORPHEUS_ERR_INVALID_ARG);
                values[2] = integer(0);
                values[1] = integer(2);
                CHECK(api->prepare(&state, &cfg) == ORPHEUS_ERR_INVALID_ARG);
                cfg.param_count = 1;
                CHECK(api->prepare(&state, &cfg) == ORPHEUS_OK);
                CHECK(state.mode == 0 && state.phase == 0);
                CHECK(api->destroy(&state) == ORPHEUS_OK);
            }
        }
    }
    ResampleState state = {0};
    const char* ids[] = {"factor", "mode"};
    OrpheusValue values[] = {integer(2), integer(1)};
    OrpheusConfig cfg = {.channels=2, .sample_rate=48000, .block_size=2,
        .param_ids=ids, .param_values=values, .param_count=2};
    CHECK(api->prepare(&state, &cfg) == ORPHEUS_OK);
    float input[] = {-0.0f, NAN, NAN, INFINITY}, output[2];
    OrpheusBuffer in = {.data=input, .format=ORPHEUS_FORMAT_F32,
        .channels=2, .frame_count=2, .frame_capacity=2, .interleaved=true};
    OrpheusBuffer out = {.data=output, .format=ORPHEUS_FORMAT_F32,
        .channels=2, .frame_capacity=1, .interleaved=true};
    const OrpheusBuffer* inputs[] = {&in};
    OrpheusBuffer* outputs[] = {&out};
    OrpheusProcessContext ctx = {.inputs=inputs, .outputs=outputs,
        .input_count=1, .output_count=1, .frame_count=2};
    CHECK(api->process(&state, &ctx) == ORPHEUS_OK);
    CHECK(memcmp(input, output, sizeof(output)) == 0);
    out.frame_capacity = 0;
    CHECK(api->process(&state, &ctx) == ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->destroy(&state) == ORPHEUS_OK);
    return 0;
}

int main(void) {
    CHECK(test_integer_decimation() == 0);
    CHECK(test_src(3, 13, 416, 48000.0) == 0);
    CHECK(test_src(13, 3, 96, 144000.0/13.0) == 0);
    CHECK(test_src(160, 147, 147, 44100.0) == 0);
    CHECK(test_src(1, 1, 32, 48000.0) == 0);
    CHECK(test_split() == 0);
    CHECK(test_window() == 0);
    return 0;
}
