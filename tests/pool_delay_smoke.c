#include "orpheus_pool_delay.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%d: %s\n", __LINE__, #expr); return 1; } } while (0)

int main(void) {
    const OrpheusComponentInterface* api = orpheus_get_interface();
    PoolDelayState state = {0};
    const char* keys[] = {"channels", "pool_size_samples"};
    OrpheusValue values[] = {
        {.type = ORPHEUS_VALUE_INT, .value.i32 = 16},
        {.type = ORPHEUS_VALUE_INT, .value.i32 = 600}
    };
    OrpheusConfig config = {.param_count = 2, .param_ids = keys, .param_values = values};
    CHECK(api->prepare(&state, &config) == ORPHEUS_OK);
    state.delays_i32[0] = 2;
    state.delays_i32[1] = 3;
    float input[32 * 16], output[32 * 16];
    OrpheusBuffer in = {.data = input}, out = {.data = output};
    const OrpheusBuffer* inputs[] = {&in};
    OrpheusBuffer* outputs[] = {&out};
    OrpheusProcessContext context = {.inputs = inputs, .outputs = outputs, .frame_count = 32};
    for (unsigned block = 0; block < 3; ++block) {
        for (unsigned n = 0; n < 32; ++n)
            for (unsigned c = 0; c < 16; ++c) input[n*16+c] = (float)(10000*c+32*block+n+1);
        if (block == 2) state.delays_i32[0] = 4;
        CHECK(api->process(&state, &context) == ORPHEUS_OK && out.frame_count == 32);
    }
    CHECK(output[0] == 63 && output[16] == 64 && output[32] == 10062 && output[48] == 10063);
    CHECK(api->reset(&state) == ORPHEUS_OK && state.cursor == 0);
    CHECK(state.delays_i32[0] == 4 && state.delays_i32[1] == 3);
    CHECK(api->process(&state, &context) == ORPHEUS_OK);
    CHECK(output[0] == 0 && output[16] == 0 && output[32] == 0 && output[48] == 0);
    uint32_t cursor = state.cursor;
    state.delays_i32[1] = -1;
    CHECK(api->process(&state, &context) == ORPHEUS_ERR_INVALID_ARG && state.cursor == cursor);
    state.delays_i32[1] = 601;
    CHECK(api->process(&state, &context) == ORPHEUS_ERR_INVALID_ARG && state.cursor == cursor);
    state.delays_i32[1] = 3;
    context.frame_count = 3;
    CHECK(api->process(&state, &context) == ORPHEUS_OK && out.frame_count == 3);
    context.frame_count = 7;
    CHECK(api->process(&state, &context) == ORPHEUS_OK && out.frame_count == 7);
    outputs[0] = NULL;
    CHECK(api->process(&state, &context) == ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->destroy(&state) == ORPHEUS_OK && !state.buffer);
    values[0].value.i32 = 65;
    CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    values[0].value.i32 = 1;
    values[1].value.i32 = 0;
    CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    values[1].type = ORPHEUS_VALUE_FLOAT;
    CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    return 0;
}
