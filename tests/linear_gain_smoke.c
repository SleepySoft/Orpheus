#include "component_state.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef orpheus_builtin_gain_State GainState;
const OrpheusComponentInterface* orpheus_get_interface(void);

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "断言失败：%s:%d\n", __FILE__, __LINE__); return 1; } } while (0)

static OrpheusSlotInfo slots[8];
static OrpheusSlotId capture_slot(void* ctx, const OrpheusSlotInfo* slot) {
    unsigned* count = (unsigned*)ctx;
    if (*count < 8) slots[*count] = *slot;
    return (*count)++;
}

int main(void) {
    const OrpheusComponentInterface* iface = orpheus_get_interface();
    GainState state = {0};
    const char* ids[] = {"gain_mode", "gain_linear", "linear_b0", "linear_a1"};
    OrpheusValue values[] = {
        {.type=ORPHEUS_VALUE_INT, .value.i32=1},
        {.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.75f},
        {.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.25f},
        {.type=ORPHEUS_VALUE_FLOAT, .value.f32=-0.75f},
    };
    OrpheusConfig config = {.sample_rate=48000, .block_size=7, .channels=3,
        .param_ids=ids, .param_values=values, .param_count=4, .state_block=&state};
    void* created = NULL;
    CHECK(iface->create(&created, &config) == ORPHEUS_OK && created == &state);
    float input[21], output[21], expected_state = 0.0f;
    OrpheusBuffer in = {.data=input, .format=ORPHEUS_FORMAT_F32, .channels=3,
                      .frame_capacity=7, .frame_count=7, .interleaved=true},
                  out = {.data=output, .format=ORPHEUS_FORMAT_F32, .channels=3,
                      .frame_capacity=7, .interleaved=true};
    const OrpheusBuffer* inputs[] = {&in};
    OrpheusBuffer* outputs[] = {&out};
    OrpheusProcessContext context = {.inputs=inputs, .outputs=outputs,
        .input_count=1, .output_count=1, .frame_count=7, .sample_rate=48000};
    CHECK(iface->prepare(&state, &config) == ORPHEUS_OK);
    unsigned slot_count = 0;
    OrpheusRegistry registry = {.ctx=&slot_count, .add=capture_slot};
    CHECK(iface->register_slots(&state, &registry) == ORPHEUS_OK && slot_count == 7);
    CHECK(strcmp(slots[0].key, "gain_db") == 0 && strcmp(slots[1].key, "channels") == 0 &&
          strcmp(slots[2].key, "smoothing_ms") == 0);
    CHECK(!(slots[0].flags & ORPHEUS_SLOT_DIRECT_WRITE));
    CHECK(strcmp(slots[4].key, "gain_linear") == 0 &&
          !(slots[4].flags & ORPHEUS_SLOT_DIRECT_WRITE));
    const OrpheusComponentDescriptor* descriptor = iface->get_descriptor();
    CHECK(descriptor->param_count == 8 && strcmp(descriptor->params[0].id, "gain_db") == 0 &&
          strcmp(descriptor->params[1].id, "channels") == 0 &&
          strcmp(descriptor->params[2].id, "smoothing_ms") == 0);
    for (unsigned step = 0; step < 5; ++step) {
        OrpheusValue target = {.type=ORPHEUS_VALUE_FLOAT, .value.f32=step ? -0.5f : 0.75f};
        CHECK(iface->set_parameter(&state, "gain_linear", &target) == ORPHEUS_OK);
        unsigned frames = step % 2 ? 3 : 7;
        if (step == 3) {
            CHECK(iface->reset(&state) == ORPHEUS_OK);
            expected_state = 0.0f;
        }
        for (unsigned i = 0; i < frames * 3; ++i) input[i] = (float)((int)i - 8) * 0.125f;
        context.frame_count = in.frame_count = frames;
        CHECK(iface->process(&state, &context) == ORPHEUS_OK);
        for (unsigned n = 0; n < frames; ++n) {
            float gain = 0.25f * target.value.f32 + expected_state;
            expected_state = 0.0f - -0.75f * gain;
            for (unsigned c = 0; c < 3; ++c) CHECK(output[n*3+c] == input[n*3+c] * gain);
        }
        CHECK(out.frame_count == frames);
    }
    OrpheusValue actual, bad = {.type=ORPHEUS_VALUE_FLOAT, .value.f32=NAN};
    CHECK(iface->set_parameter(&state, "gain_linear", &bad) != ORPHEUS_OK);
    bad.type = ORPHEUS_VALUE_INT; bad.value.i32 = 1;
    CHECK(iface->set_parameter(&state, "gain_linear", &bad) != ORPHEUS_OK);
    CHECK(iface->get_parameter(&state, "gain_linear", &actual) == ORPHEUS_OK);
    CHECK(actual.value.f32 == -0.5f);
    CHECK(iface->set_parameter(&state, "gain_db", &actual) != ORPHEUS_OK);
    values[2].value.f32 = 1.0f; values[3].value.f32 = 0.0f;
    CHECK(iface->prepare(&state, &config) == ORPHEUS_OK);
    const float special[] = {0.0f, -0.0f, INFINITY, -INFINITY, NAN, 0x1p80f, -0x1p80f};
    for (unsigned sign = 0; sign < 3; ++sign) {
        OrpheusValue target = {.type=ORPHEUS_VALUE_FLOAT,
            .value.f32=sign == 0 ? 0.0f : sign == 1 ? -1.0f : -0.0f};
        CHECK(iface->set_parameter(&state, "gain_linear", &target) == ORPHEUS_OK);
        for (unsigned i = 0; i < 21; ++i) input[i] = special[i % 7];
        context.frame_count = in.frame_count = 7;
        CHECK(iface->process(&state, &context) == ORPHEUS_OK);
        for (unsigned i = 0; i < 21; ++i) {
            float expected = input[i] * target.value.f32;
            CHECK(isnan(expected) ? isnan(output[i]) : memcmp(&expected, &output[i], sizeof(float)) == 0);
        }
    }
    values[3].value.f32 = 1.0f;
    CHECK(iface->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    values[3].value.f32 = NAN;
    CHECK(iface->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    config.param_count = 0;
    CHECK(iface->prepare(&state, &config) == ORPHEUS_OK);
    actual.type=ORPHEUS_VALUE_FLOAT; actual.value.f32=-6.0f;
    CHECK(iface->set_parameter(&state, "gain_db", &actual) == ORPHEUS_OK);
    CHECK(iface->reset(&state) == ORPHEUS_OK);
    CHECK(iface->get_parameter(&state, "gain_db", &actual) == ORPHEUS_OK && actual.value.f32 == 0.0f);
    CHECK(iface->get_parameter(&state, "gain_mode", &actual) == ORPHEUS_OK && actual.value.i32 == 0);
    const char* channel_ids[] = {"gain_mode", "gains"};
    OrpheusValue channel_values[] = {
        {.type=ORPHEUS_VALUE_INT, .value.i32=2},
        {.type=ORPHEUS_VALUE_STRING, .value.str="0, -0.5, 2"},
    };
    config.param_ids = channel_ids; config.param_values = channel_values; config.param_count = 2;
    CHECK(iface->prepare(&state, &config) == ORPHEUS_OK);
    slot_count = 0;
    CHECK(iface->register_slots(&state, &registry) == ORPHEUS_OK && slot_count == 8);
    CHECK(slots[7].kind == ORPHEUS_SLOT_BULK && slots[7].count == 3 &&
          slots[7].type == ORPHEUS_VALUE_FLOAT && slots[7].size == sizeof(float) &&
          slots[7].offset == offsetof(GainState, algorithm) +
              offsetof(orpheus_builtin_gain_AlgorithmState, per_channel_gains) &&
          (slots[7].flags & ORPHEUS_SLOT_DOUBLE_BUFFERED));
    const float gains[] = {0.0f, -0.5f, 2.0f};
    for (unsigned i = 0; i < 21; ++i) input[i] = special[i % 7];
    CHECK(iface->process(&state, &context) == ORPHEUS_OK);
    for (unsigned i = 0; i < 21; ++i) {
        float expected = input[i]*gains[i % 3];
        CHECK(isnan(expected) ? isnan(output[i]) : !memcmp(&expected, &output[i], sizeof(float)));
    }
    CHECK(iface->reset(&state) == ORPHEUS_OK &&
          !memcmp(state.algorithm.per_channel_gains, gains, sizeof(gains)));
    CHECK(iface->set_parameter(&state, "gain_linear", &actual) == ORPHEUS_ERR_INVALID_ARG);
    actual.type = ORPHEUS_VALUE_FLOAT;
    actual.value.f32 = 1.0f;
    CHECK(iface->set_parameter(&state, "gain_linear", &actual) == ORPHEUS_ERR_UNSUPPORTED);
    const char* invalid[] = {"1,2", "1,2,3,4", "1,2,nan", "1,2,inf", "1,2,3junk", "1,,2", "1,2,3,"};
    for (unsigned i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
        channel_values[1].value.str = invalid[i];
        CHECK(iface->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    }
    channel_values[1].value.str = "";
    CHECK(iface->prepare(&state, &config) == ORPHEUS_OK);
    for (unsigned i = 0; i < 3; ++i) CHECK(state.algorithm.per_channel_gains[i] == 1.0f);
    config.channels = 33;
    CHECK(iface->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    CHECK(iface->destroy(&state) == ORPHEUS_OK);
    return 0;
}
