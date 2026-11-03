#include "orpheus_delay_line.h"
#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%d: %s\n", __LINE__, #expr); return 1; } } while (0)

static int slots_seen;
static OrpheusSlotId check_slot(void* ctx, const OrpheusSlotInfo* slot) {
    (void)ctx;
    if (strcmp(slot->key, "delay_samples") == 0 &&
        slot->type == ORPHEUS_VALUE_INT && slot->count == 1 &&
        !(slot->flags & ORPHEUS_SLOT_DIRECT_WRITE)) ++slots_seen;
    return 0;
}

static int test_integer_vector(const OrpheusComponentInterface* iface) {
    const char* ids[] = {"channels", "max_delay_samples", "delay_vector_mode", "initial_delays_i32"};
    OrpheusValue values[] = {
        {.type=ORPHEUS_VALUE_INT, .value.i32=3},
        {.type=ORPHEUS_VALUE_INT, .value.i32=16},
        {.type=ORPHEUS_VALUE_INT, .value.i32=1},
        {.type=ORPHEUS_VALUE_STRING, .value.str="0,3,16"}
    };
    OrpheusConfig config = {.sample_rate=48000, .block_size=7, .param_count=4,
                           .param_ids=ids, .param_values=values};
    DelayLineState state = {0};
    CHECK(iface->prepare(&state, &config) == ORPHEUS_OK);
    float input[21], output[21], history[512][3] = {{0}};
    OrpheusBuffer in = {.data=input, .channels=3, .format=ORPHEUS_FORMAT_F32, .frame_capacity=7};
    OrpheusBuffer out = {.data=output, .channels=3, .format=ORPHEUS_FORMAT_F32, .frame_capacity=7};
    const OrpheusBuffer* inputs[] = {&in};
    OrpheusBuffer* outputs[] = {&out};
    OrpheusProcessContext context = {.inputs=inputs, .outputs=outputs};
    for (unsigned pass = 0; pass < 2; ++pass) {
        unsigned cursor = 0;
        if (pass) {
            CHECK(iface->reset(&state) == ORPHEUS_OK);
            CHECK(state.delay_vector_mode == 1 && state.delays_i32[0] == 0 &&
                  state.delays_i32[1] == 3 && state.delays_i32[2] == 16);
            memset(history, 0, sizeof(history));
        }
        for (unsigned block = 0; block < 64; ++block) {
            unsigned frames = block % 2 ? 3 : 7;
            for (unsigned n = 0; n < frames; ++n)
                for (unsigned c = 0; c < 3; ++c)
                    history[cursor+n][c] = input[n*3+c] = (float)(cursor+n+1) * (float)(c+1);
            context.frame_count = in.frame_count = frames;
            CHECK(iface->process(&state, &context) == ORPHEUS_OK);
            for (unsigned n = 0; n < frames; ++n)
                for (unsigned c = 0; c < 3; ++c) {
                    unsigned d = (unsigned)state.delays_i32[c];
                    CHECK(output[n*3+c] == (cursor+n >= d ? history[cursor+n-d][c] : 0.0f));
                }
            cursor += frames;
        }
    }
    CHECK(iface->destroy(&state) == ORPHEUS_OK);
    const char* invalid[] = {"1,2", "1,2,3,4", "1.0,2,3", "1,2,17", "1,-1,3",
                            "1,2,3x", "1,2,", "1,,3", "2147483648,2,3"};
    for (unsigned i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
        DelayLineState rejected = {0};
        values[3].value.str = invalid[i];
        CHECK(iface->prepare(&rejected, &config) == ORPHEUS_ERR_INVALID_ARG);
        CHECK(rejected.buffer == NULL);
    }
    values[3].value.str = "";
    memset(&state, 0, sizeof(state));
    CHECK(iface->prepare(&state, &config) == ORPHEUS_OK);
    CHECK(state.delays_i32[0] == 0 && state.delays_i32[1] == 0 && state.delays_i32[2] == 0);
    CHECK(iface->destroy(&state) == ORPHEUS_OK);
    return 0;
}

int main(void) {
    const OrpheusComponentInterface* iface = orpheus_get_interface();
    const char* ids[] = {"channels", "max_delay_samples", "delays_samples", "delay_samples"};
    OrpheusValue values[] = {
        {.type=ORPHEUS_VALUE_INT, .value.i32=3},
        {.type=ORPHEUS_VALUE_INT, .value.i32=16},
        {.type=ORPHEUS_VALUE_STRING, .value.str="1,3,7"},
        {.type=ORPHEUS_VALUE_INT, .value.i32=5}
    };
    DelayLineState state = {0};
    OrpheusConfig config = {
        .sample_rate=48000, .block_size=7, .channels=3,
        .param_count=4, .param_ids=ids, .param_values=values, .state_block=&state
    };
    void* instance = NULL;
    float input[21], output[21], history[512][3] = {{0}};
    const unsigned legacy[] = {1, 3, 7};
    OrpheusBuffer in = {.data=input, .channels=3, .format=ORPHEUS_FORMAT_F32, .frame_capacity=7};
    OrpheusBuffer out = {.data=output, .channels=3, .format=ORPHEUS_FORMAT_F32, .frame_capacity=7};
    const OrpheusBuffer* inputs[] = {&in};
    OrpheusBuffer* outputs[] = {&out};
    OrpheusProcessContext context = {.inputs=inputs, .outputs=outputs};
    OrpheusValue value = {.type=ORPHEUS_VALUE_INT}, actual;
    CHECK(iface->create(&instance, &config) == ORPHEUS_OK && instance == &state);
    CHECK(iface->prepare(instance, &config) == ORPHEUS_OK);
    OrpheusRegistry registry = {.ctx=NULL, .add=check_slot};
    CHECK(iface->register_slots(instance, &registry) == ORPHEUS_OK && slots_seen == 1);
    for (unsigned pass = 0; pass < 2; ++pass) {
        unsigned cursor = 0;
        if (pass) {
            CHECK(iface->reset(instance) == ORPHEUS_OK);
            CHECK(iface->get_parameter(instance, "delay_samples", &actual) == ORPHEUS_OK);
            CHECK(actual.value.i32 == -1);
            memset(history, 0, sizeof(history));
        }
        for (unsigned block = 0; block < 64; ++block) {
            unsigned frames = block % 2 ? 3 : 7;
            int delay = block < 8 ? 5 : block < 16 ? 0 : block < 24 ? 16
                      : block < 32 ? 1 : -1;
            value.value.i32 = delay;
            CHECK(iface->set_parameter(instance, "delay_samples", &value) == ORPHEUS_OK);
            for (unsigned n = 0; n < frames; ++n)
                for (unsigned c = 0; c < 3; ++c)
                    history[cursor+n][c] = input[n*3+c] = (float)(cursor+n+1) * (float)(c+1);
            context.frame_count = in.frame_count = frames;
            CHECK(iface->process(instance, &context) == ORPHEUS_OK);
            CHECK(out.frame_count == frames);
            for (unsigned n = 0; n < frames; ++n)
                for (unsigned c = 0; c < 3; ++c) {
                    unsigned d = delay < 0 ? legacy[c] : (unsigned)delay;
                    float expected = cursor+n >= d ? history[cursor+n-d][c] : 0.0f;
                    CHECK(output[n*3+c] == expected);
                }
            cursor += frames;
        }
        value.value.i32 = 17;
        CHECK(iface->set_parameter(instance, "delay_samples", &value) == ORPHEUS_ERR_INVALID_ARG);
        value.value.i32 = -2;
        CHECK(iface->set_parameter(instance, "delay_samples", &value) == ORPHEUS_ERR_INVALID_ARG);
        value.type = ORPHEUS_VALUE_FLOAT; value.value.f32 = 1.0f;
        CHECK(iface->set_parameter(instance, "delay_samples", &value) == ORPHEUS_ERR_INVALID_ARG);
        CHECK(iface->get_parameter(instance, "delay_samples", &actual) == ORPHEUS_OK);
        CHECK(actual.value.i32 == -1);
        value.type = ORPHEUS_VALUE_INT;
    }
    value.value.i32 = 5;
    CHECK(iface->set_parameter(instance, "delay_samples", &value) == ORPHEUS_OK);
    CHECK(iface->reset(instance) == ORPHEUS_OK);
    CHECK(iface->get_parameter(instance, "delay_samples", &actual) == ORPHEUS_OK);
    CHECK(actual.value.i32 == 5);
    context.frame_count = in.frame_count = 7;
    CHECK(iface->process(instance, &context) == ORPHEUS_OK);
    for (unsigned n = 0; n < 7; ++n)
        for (unsigned c = 0; c < 3; ++c)
            CHECK(output[n*3+c] == (n < 5 ? 0.0f : input[(n-5)*3+c]));
    CHECK(iface->destroy(instance) == ORPHEUS_OK);
    DelayLineState invalid = {0};
    values[3].value.i32 = 17;
    CHECK(iface->prepare(&invalid, &config) == ORPHEUS_ERR_INVALID_ARG);
    values[3].type = ORPHEUS_VALUE_FLOAT;
    CHECK(iface->prepare(&invalid, &config) == ORPHEUS_ERR_INVALID_ARG);
    config.param_count = 3;
    CHECK(iface->prepare(&invalid, &config) == ORPHEUS_OK);
    CHECK(invalid.delay_samples == -1);
    CHECK(iface->destroy(&invalid) == ORPHEUS_OK);
    CHECK(test_integer_vector(iface) == 0);
    return 0;
}
