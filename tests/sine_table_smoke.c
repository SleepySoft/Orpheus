#include "orpheus_sine_mod.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%d: %s\n", __LINE__, #expr); return 1; } } while (0)

int main(void) {
    const OrpheusComponentInterface* api = orpheus_get_interface();
    SineModState state = {0};
    const char* keys[] = {"modulation_mode", "table_length", "carrier_table"};
    OrpheusValue values[] = {
        {.type = ORPHEUS_VALUE_INT, .value.i32 = 1},
        {.type = ORPHEUS_VALUE_INT, .value.i32 = 5},
        {.type = ORPHEUS_VALUE_STRING, .value.str = "0, -0, -1, 0.5, 1"}
    };
    OrpheusConfig config = {.channels = 3, .sample_rate = 48000, .block_size = 32,
        .param_count = 3, .param_ids = keys, .param_values = values};
    CHECK(api->prepare(&state, &config) == ORPHEUS_OK);
    float input[32*3], output[32*3];
    OrpheusBuffer in = {.data = input}, out = {.data = output};
    const OrpheusBuffer* inputs[] = {&in};
    OrpheusBuffer* outputs[] = {&out};
    OrpheusProcessContext context = {.inputs = inputs, .outputs = outputs};
    const unsigned blocks[] = {3, 7, 32, 1};
    unsigned absolute = 0;
    for (unsigned block = 0; block < 4; ++block) {
        context.frame_count = blocks[block];
        for (unsigned n = 0; n < context.frame_count; ++n)
            for (unsigned c = 0; c < 3; ++c) input[n*3+c] = (float)(absolute+n+c+1);
        CHECK(api->process(&state, &context) == ORPHEUS_OK && out.frame_count == context.frame_count);
        for (unsigned n = 0; n < context.frame_count; ++n)
            for (unsigned c = 0; c < 3; ++c) {
                float expected = input[n*3+c] * state.carrier_table[(absolute+n)%5];
                CHECK(memcmp(&expected, &output[n*3+c], sizeof(float)) == 0);
            }
        absolute += context.frame_count;
    }
    state.freq_hz = 17;
    state.depth = 0.25f;
    CHECK(api->reset(&state) == ORPHEUS_OK && state.table_index == 0);
    CHECK(state.freq_hz == 17 && state.depth == 0.25f && state.table_length == 5);
    context.frame_count = 3;
    input[0] = INFINITY; input[3] = 2; input[6] = 2;
    CHECK(api->process(&state, &context) == ORPHEUS_OK);
    CHECK(isnan(output[0]) && output[3] == 0 && signbit(output[3]) && output[6] == -2);
    CHECK(api->reset(&state) == ORPHEUS_OK);
    out.data = input;
    input[0] = 2; input[3] = 2; input[6] = 2;
    CHECK(api->process(&state, &context) == ORPHEUS_OK);
    CHECK(input[0] == 0 && input[3] == 0 && signbit(input[3]) && input[6] == -2);
    const char* bad[] = {"", "1,2", "1,2,3,4,5,6", "1,2,3,4,5,", "1,,3,4,5",
                         "nan,2,3,4,5", "inf,2,3,4,5", "1x,2,3,4,5", "1e99,2,3,4,5"};
    for (unsigned n = 0; n < sizeof(bad)/sizeof(bad[0]); ++n) {
        values[2].value.str = bad[n];
        CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    }
    values[1].value.i32 = 1025;
    CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    values[1].value.i32 = 0;
    CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    values[1].type = ORPHEUS_VALUE_FLOAT;
    CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    config.param_count = 0;
    CHECK(api->prepare(&state, &config) == ORPHEUS_OK && state.modulation_mode == 0);
    out.data = output;
    context.frame_count = 3;
    for (unsigned n = 0; n < 9; ++n) input[n] = 0.5f;
    float phase = 0;
    CHECK(api->process(&state, &context) == ORPHEUS_OK);
    for (unsigned n = 0; n < 9; ++n) {
        float expected = input[n] * (1.0f + sinf(phase));
        CHECK(output[n] == expected);
        phase += state.phase_inc;
    }
    return 0;
}
