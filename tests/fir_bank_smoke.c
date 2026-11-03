#include "orpheus_fir_bank.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%d: %s\n", __LINE__, #expr); return 1; } } while (0)

static OrpheusSlotInfo coefficient_slot;

static OrpheusSlotId capture(void* ctx, const OrpheusSlotInfo* slot) {
    (void)ctx;
    if (slot->kind == ORPHEUS_SLOT_BULK) coefficient_slot = *slot;
    return 0;
}

int main(void) {
    const OrpheusComponentInterface* api = orpheus_get_interface();
    FirBankState state = {0};
    const char* keys[] = {"num_taps", "coefs", "coefs_mode", "taps_per_channel"};
    OrpheusValue values[] = {
        {.type=ORPHEUS_VALUE_INT, .value.i32=3},
        {.type=ORPHEUS_VALUE_STRING, .value.str="1,0.5,0.25,2,99,99,99,99,99"},
        {.type=ORPHEUS_VALUE_STRING, .value.str="per_channel"},
        {.type=ORPHEUS_VALUE_STRING, .value.str="3,1,0"}
    };
    OrpheusConfig config = {.channels=3, .param_count=4, .param_ids=keys, .param_values=values};
    CHECK(api->prepare(&state, &config) == ORPHEUS_OK);
    OrpheusRegistry registry = {.add=capture};
    CHECK(api->register_slots(&state, &registry) == ORPHEUS_OK);
    CHECK(coefficient_slot.count == 9 && coefficient_slot.size == sizeof(float));
    CHECK(strcmp(coefficient_slot.key, "coefs_per_channel") == 0);
    CHECK(coefficient_slot.flags & ORPHEUS_SLOT_DOUBLE_BUFFERED);
    state.coefs[4] = NAN;
    float data[9] = {1,2,NAN, 0,3,NAN, 0,4,NAN};
    OrpheusBuffer in = {.data=data}, out = {.data=data};
    const OrpheusBuffer* inputs[] = {&in};
    OrpheusBuffer* outputs[] = {&out};
    OrpheusProcessContext context = {.inputs=inputs, .outputs=outputs, .frame_count=3};
    CHECK(api->process(&state, &context) == ORPHEUS_OK);
    const float expected[] = {1,4,0, 0.5f,6,0, 0.25f,8,0};
    CHECK(memcmp(data, expected, sizeof(data)) == 0);
    float frozen[9];
    memcpy(frozen, state.history, sizeof(frozen));
    uint32_t positions[3];
    memcpy(positions, state.positions, sizeof(positions));
    OrpheusValue enable = {.type=ORPHEUS_VALUE_INT, .value.i32=0};
    CHECK(api->set_parameter(&state, "processing_enabled", &enable) == ORPHEUS_OK);
    inputs[0] = NULL;
    CHECK(api->process(&state, &context) == ORPHEUS_OK);
    for (unsigned i = 0; i < 9; ++i) CHECK(data[i] == 0 && !signbit(data[i]));
    CHECK(memcmp(frozen, state.history, sizeof(frozen)) == 0);
    CHECK(memcmp(positions, state.positions, sizeof(positions)) == 0);
    CHECK(api->reset(&state) == ORPHEUS_OK && state.processing_enabled == 0 && state.coefs[0] == 1);
    for (unsigned i = 0; i < 9; ++i) CHECK(state.history[i] == 0);
    const char* invalid[] = {"1,,2", "1,2", "1,2,3,", "1,2,nan", "1,2,inf", "1,2,3oops"};
    values[2].value.str = "shared";
    for (unsigned i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
        values[1].value.str = invalid[i];
        CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    }
    values[1].value.str = "";
    const char* bad_lengths[] = {"1,2", "1,2,3,", "-1,1,1", "1,2,4", "1,1.5,2", "1,,2"};
    for (unsigned i = 0; i < sizeof(bad_lengths)/sizeof(bad_lengths[0]); ++i) {
        values[3].value.str = bad_lengths[i];
        CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    }
    values[3].value.str = "";
    values[1].value.str = "1e20,-1e20,1"; config.channels = 1;
    CHECK(api->prepare(&state, &config) == ORPHEUS_OK);
    float rounding[3] = {1,1,1};
    in.data = rounding; out.data = rounding; inputs[0] = &in;
    CHECK(api->process(&state, &context) == ORPHEUS_OK && rounding[2] == 0);
    values[1].value.str = "";
    values[0].value.i32 = 530; values[2].value.str = "per_channel"; config.channels = 42;
    CHECK(api->prepare(&state, &config) == ORPHEUS_OK);
    CHECK(api->register_slots(&state, &registry) == ORPHEUS_OK && coefficient_slot.count == 22260);
    float wide[64] = {0};
    wide[41] = 0.75f;
    in.data = wide; out.data = wide; inputs[0] = &in; context.frame_count = 1;
    CHECK(api->process(&state, &context) == ORPHEUS_OK && wide[41] == 0.75f);
    for (unsigned i = 0; i < 41; ++i) CHECK(wide[i] == 0);
    CHECK(api->reset(&state) == ORPHEUS_OK);
    state.coefs[41*530+529] = 0.5f;
    for (unsigned frame = 0; frame < 533; ++frame) {
        memset(wide, 0, sizeof(wide));
        if (frame == 0) wide[41] = 0.75f;
        CHECK(api->process(&state, &context) == ORPHEUS_OK);
        CHECK(wide[41] == (frame == 0 ? 0.75f : frame == 529 ? 0.375f : 0));
        for (unsigned i = 0; i < 41; ++i) CHECK(wide[i] == 0);
    }
    values[0].value.i32 = 512; config.channels = 64;
    CHECK(api->prepare(&state, &config) == ORPHEUS_OK);
    values[0].value.i32 = 513;
    CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    values[0].value.i32 = 4096; values[2].value.str = "shared";
    CHECK(api->prepare(&state, &config) == ORPHEUS_OK);
    values[0].value.i32 = 4097;
    CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    values[0].value.i32 = 1; config.channels = 65;
    CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->destroy(&state) == ORPHEUS_OK);
    CHECK(api->destroy(&state) == ORPHEUS_OK);
    return 0;
}
