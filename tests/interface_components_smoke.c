#include "orpheus/builtin/constant_source/generated/component_state.h"
#include "orpheus/builtin/linear_gain/generated/component_state.h"
#include "orpheus/builtin/null_sink/generated/component_state.h"
#include <stdio.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%d: %s\n", __LINE__, #expr); return 1; } } while (0)

const OrpheusComponentInterface* test_constant_source(void);
const OrpheusComponentInterface* test_linear_gain(void);
const OrpheusComponentInterface* test_null_sink(void);

int main(void) {
    const OrpheusComponentInterface* source = test_constant_source();
    const OrpheusComponentInterface* gain = test_linear_gain();
    const OrpheusComponentInterface* sink = test_null_sink();
    orpheus_builtin_constant_source_State source_state = {0};
    orpheus_builtin_linear_gain_State gain_state = {0};
    orpheus_builtin_null_sink_State sink_state = {0};
    const char* source_ids[] = {"channels", "value"};
    const char* gain_ids[] = {"channels", "gain"};
    OrpheusValue params[] = {
        {.type=ORPHEUS_VALUE_INT, .value.i32=1},
        {.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.25f},
    };
    OrpheusConfig config = {.sample_rate=48000, .block_size=8, .channels=1,
        .param_ids=source_ids, .param_values=params, .param_count=2};
    CHECK(source->prepare(&source_state, &config) == ORPHEUS_OK);
    config.param_ids = gain_ids;
    params[1].value.f32 = 2.0f;
    CHECK(gain->prepare(&gain_state, &config) == ORPHEUS_OK);
    config.param_count = 1;
    CHECK(sink->prepare(&sink_state, &config) == ORPHEUS_OK);

    float source_samples[8] = {0}, gain_samples[8] = {0};
    OrpheusBuffer source_buffer = {.data=source_samples, .format=ORPHEUS_FORMAT_F32,
        .channels=1, .frame_capacity=8, .interleaved=true};
    OrpheusBuffer gain_buffer = {.data=gain_samples, .format=ORPHEUS_FORMAT_F32,
        .channels=1, .frame_capacity=8, .interleaved=true};
    OrpheusBuffer* source_outputs[] = {&source_buffer};
    OrpheusBuffer* gain_outputs[] = {&gain_buffer};
    const OrpheusBuffer* gain_inputs[] = {&source_buffer};
    const OrpheusBuffer* sink_inputs[] = {&gain_buffer};
    for (unsigned pass = 0; pass < 2; ++pass) {
        const unsigned frames = pass ? 3 : 8;
        if (pass) {
            params[1].value.f32 = -0.25f;
            CHECK(source->set_parameter(&source_state, "value", &params[1]) == ORPHEUS_OK);
            params[1].value.f32 = 3.0f;
            CHECK(gain->set_parameter(&gain_state, "gain", &params[1]) == ORPHEUS_OK);
        }
        OrpheusProcessContext context = {.outputs=source_outputs, .output_count=1,
            .frame_count=frames, .valid_frames=frames, .sample_rate=48000,
            .trigger_input=UINT32_MAX};
        CHECK(source->process(&source_state, &context) == ORPHEUS_OK);
        CHECK(source_buffer.frame_count == frames);
        context.inputs = gain_inputs;
        context.input_count = 1;
        context.outputs = gain_outputs;
        context.trigger_input = 0;
        CHECK(gain->process(&gain_state, &context) == ORPHEUS_OK);
        CHECK(gain_buffer.frame_count == frames);
        for (unsigned i = 0; i < frames; ++i)
            CHECK(gain_samples[i] == (pass ? -0.75f : 0.5f));
        if (pass)
            for (unsigned i = frames; i < 8; ++i) CHECK(gain_samples[i] == 0.5f);
        context.inputs = sink_inputs;
        context.outputs = NULL;
        context.output_count = 0;
        CHECK(sink->process(&sink_state, &context) == ORPHEUS_OK);
    }
    return 0;
}
