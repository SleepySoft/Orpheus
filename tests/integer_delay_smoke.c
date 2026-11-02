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
    return 0;
}
