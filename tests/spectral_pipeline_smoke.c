#include "orpheus_rfft.h"
#include "orpheus_ifft.h"
#include "orpheus_overlap_add.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

const OrpheusComponentInterface* spectral_test_rfft(void);
const OrpheusComponentInterface* spectral_test_ifft(void);
const OrpheusComponentInterface* spectral_test_overlap_add(void);

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "检查失败，行%d: %s\n", __LINE__, #expr); return 1; \
} } while (0)

static OrpheusBuffer buffer(float* data, uint32_t frames, uint32_t channels) {
    OrpheusBuffer result = {.data=data, .format=ORPHEUS_FORMAT_F32,
        .channels=channels, .frame_capacity=frames, .frame_count=frames, .interleaved=true};
    return result;
}

static int fft_batches(void) {
    const OrpheusComponentInterface* fft = spectral_test_rfft();
    const OrpheusComponentInterface* inverse = spectral_test_ifft();
    RfftState fs = {0};
    IfftState is = {0};
    const char* ids[] = {"fft_size", "output_mode"};
    OrpheusValue values[] = {
        {.type=ORPHEUS_VALUE_INT, .value.i32=256},
        {.type=ORPHEUS_VALUE_STRING, .value.str="power"}
    };
    OrpheusConfig config = {.channels=2, .block_size=768, .sample_rate=48000,
        .param_count=2, .param_ids=ids, .param_values=values};
    CHECK(fft->prepare(&fs, &config) == ORPHEUS_OK);
    CHECK(inverse->prepare(&is, &config) == ORPHEUS_OK);
    float input[1536], power[1536], spectrum[1536], restored[1536];
    for (uint32_t frame = 0; frame < 768; ++frame) {
        input[2 * frame] = 0.1f * (float)(frame / 256 + 1)
            + 0.3f * sinf(6.28318530718f * 7 * (float)frame / 256)
            + (frame % 2 ? -0.07f : 0.07f);
        input[2 * frame + 1] = -0.04f * (float)(frame / 256 + 1)
            + 0.2f * cosf(6.28318530718f * 13 * (float)frame / 256);
    }
    OrpheusBuffer in = buffer(input, 768, 2), old = buffer(power, 768, 2);
    OrpheusBuffer spec = buffer(spectrum, 768, 2), out = buffer(restored, 768, 2);
    const OrpheusBuffer* inputs[] = {&in};
    OrpheusBuffer* outputs[] = {&old, &spec};
    OrpheusProcessContext ctx = {.inputs=inputs, .input_count=1,
        .outputs=outputs, .output_count=2, .frame_count=768};
    CHECK(fft->process(&fs, &ctx) == ORPHEUS_OK);
    CHECK(spec.frame_count == 768 && old.frame_count == 768);
    /* 独立DFT检查布局、虚部符号、DC和Nyquist，不仅依赖互逆抵消。 */
    for (uint32_t batch = 0; batch < 3; ++batch) {
        for (uint32_t channel = 0; channel < 2; ++channel) {
            for (uint32_t bin = 0; bin <= 128; ++bin) {
                double re = 0, im = 0;
                for (uint32_t sample = 0; sample < 256; ++sample) {
                    double angle = -6.283185307179586 * bin * sample / 256;
                    double value = input[2 * (256 * batch + sample) + channel];
                    re += value * cos(angle);
                    im += value * sin(angle);
                }
                CHECK(fabs(spectrum[2 * (256 * batch + bin) + channel] - re) < 0.00005);
                if (bin > 0 && bin < 128)
                    CHECK(fabs(spectrum[2 * (256 * batch + 256 - bin) + channel] - im) < 0.00005);
                CHECK(fabs(power[2 * (256 * batch + bin) + channel] - (re * re + im * im)) < 0.005);
            }
        }
    }
    const OrpheusBuffer* inverse_inputs[] = {NULL, &spec};
    OrpheusBuffer* inverse_outputs[] = {&out};
    ctx.inputs = inverse_inputs;
    ctx.input_count = 2;
    ctx.outputs = inverse_outputs;
    ctx.output_count = 1;
    ctx.frame_count = 128;
    CHECK(inverse->process(&is, &ctx) == ORPHEUS_OK);
    CHECK(out.frame_count == 768);
    for (uint32_t i = 0; i < 1536; ++i) CHECK(fabsf(restored[i] - input[i]) < 0.000002f);
    inverse_inputs[0] = &spec;
    CHECK(inverse->process(&is, &ctx) == ORPHEUS_ERR_INVALID_ARG);
    inverse_inputs[0] = NULL;
    spec.frame_count = 767;
    CHECK(inverse->process(&is, &ctx) == ORPHEUS_ERR_INVALID_ARG);
    spec.frame_count = 768;
    out.frame_capacity = 767;
    CHECK(inverse->process(&is, &ctx) == ORPHEUS_ERR_INVALID_ARG);
    out.frame_capacity = 768;
    in.frame_count = 767;
    ctx.inputs = inputs;
    ctx.input_count = 1;
    ctx.outputs = outputs;
    ctx.output_count = 2;
    CHECK(fft->process(&fs, &ctx) == ORPHEUS_ERR_INVALID_ARG);
    config.block_size = 767;
    CHECK(fft->prepare(&fs, &config) == ORPHEUS_ERR_INVALID_ARG);
    values[0].value.i32 = 255;
    CHECK(inverse->prepare(&is, &config) == ORPHEUS_ERR_INVALID_ARG);
    values[0].value.i32 = 0;
    config.block_size = 128;
    CHECK(inverse->prepare(&is, &config) == ORPHEUS_OK);
    spec.frame_count = 256;
    ctx.inputs = inverse_inputs;
    ctx.input_count = 2;
    ctx.outputs = inverse_outputs;
    ctx.output_count = 1;
    CHECK(inverse->process(&is, &ctx) == ORPHEUS_OK);
    CHECK(out.frame_count == 256);
    for (uint32_t i = 0; i < 512; ++i) CHECK(fabsf(restored[i] - input[i]) < 0.000002f);
    return 0;
}

static int overlap_history(void) {
    const OrpheusComponentInterface* ola = spectral_test_overlap_add();
    const char* ids[] = {"frame_size", "hop_size", "num_frames"};
    OrpheusValue values[] = {
        {.type=ORPHEUS_VALUE_INT, .value.i32=4},
        {.type=ORPHEUS_VALUE_INT, .value.i32=2},
        {.type=ORPHEUS_VALUE_INT, .value.i32=3}
    };
    OverlapAddState storage;
    void* state = NULL;
    OrpheusConfig config = {.channels=2, .block_size=12, .sample_rate=48000,
        .state_block=&storage, .param_count=3, .param_ids=ids, .param_values=values};
    CHECK(ola->create(&state, &config) == ORPHEUS_OK);
    CHECK(ola->prepare(state, &config) == ORPHEUS_OK);
    const float frames[] = {1, 2, 3, 4, 10, 20, 30, 40, 100, 200, 300, 400};
    float input[24], output[12];
    for (uint32_t i = 0; i < 12; ++i) {
        input[2 * i] = frames[i];
        input[2 * i + 1] = -frames[i];
    }
    OrpheusBuffer in = buffer(input, 12, 2), out = buffer(output, 6, 2);
    const OrpheusBuffer* inputs[] = {&in};
    OrpheusBuffer* outputs[] = {&out};
    OrpheusProcessContext ctx = {.inputs=inputs, .input_count=1,
        .outputs=outputs, .output_count=1, .frame_count=12};
    CHECK(ola->process(state, &ctx) == ORPHEUS_OK);
    const float expected[] = {1, 2, 13, 24, 130, 240};
    for (uint32_t i = 0; i < 6; ++i) {
        CHECK(output[2 * i] == expected[i]);
        CHECK(output[2 * i + 1] == -expected[i]);
    }
    memset(input, 0, sizeof(input));
    out.frame_capacity = 5;
    CHECK(ola->process(state, &ctx) == ORPHEUS_ERR_INVALID_ARG);
    out.frame_capacity = 6;
    CHECK(ola->process(state, &ctx) == ORPHEUS_OK);
    CHECK(out.frame_count == 6);
    CHECK(output[0] == 300 && output[1] == -300);
    CHECK(output[2] == 400 && output[3] == -400);
    for (uint32_t i = 4; i < 12; ++i) CHECK(output[i] == 0);
    for (uint32_t i = 0; i < 12; ++i) {
        input[2 * i] = frames[i];
        input[2 * i + 1] = -frames[i];
    }
    CHECK(ola->process(state, &ctx) == ORPHEUS_OK);
    CHECK(storage.history[0] == 300 && storage.history[2] == 400);
    CHECK(ola->reset(state) == ORPHEUS_OK);
    memset(input, 0, sizeof(input));
    CHECK(ola->process(state, &ctx) == ORPHEUS_OK);
    for (uint32_t i = 0; i < 12; ++i) CHECK(output[i] == 0);
    values[1].value.i32 = 5;
    CHECK(ola->prepare(state, &config) == ORPHEUS_ERR_INVALID_ARG);
    CHECK(ola->destroy(state) == ORPHEUS_OK);
    values[1].value.i32 = 4;
    values[2].value.i32 = 1;
    config.block_size = 4;
    config.state_block = NULL;
    CHECK(ola->create(&state, &config) == ORPHEUS_OK);
    CHECK(ola->prepare(state, &config) == ORPHEUS_OK);
    in.frame_count = 4;
    ctx.frame_count = 4;
    for (uint32_t i = 0; i < 8; ++i) input[i] = (float)i;
    CHECK(ola->process(state, &ctx) == ORPHEUS_OK);
    CHECK(out.frame_count == 4);
    for (uint32_t i = 0; i < 8; ++i) CHECK(output[i] == input[i]);
    CHECK(ola->destroy(state) == ORPHEUS_OK);
    return 0;
}

int main(void) {
    CHECK(fft_batches() == 0);
    CHECK(overlap_history() == 0);
    puts("频域批次、布局、容量保护和OLA历史检查通过");
    return 0;
}
