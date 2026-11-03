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
    OrpheusBuffer in = {.data = input, .channels = 16, .frame_count = 32, .frame_capacity = 32,
                        .format = ORPHEUS_FORMAT_F32, .interleaved = true};
    OrpheusBuffer out = in;
    out.data = output;
    const OrpheusBuffer* inputs[] = {&in};
    OrpheusBuffer* outputs[] = {&out};
    OrpheusProcessContext context = {.inputs = inputs, .outputs = outputs, .input_count = 1, .output_count = 1, .frame_count = 32};
    for (unsigned block = 0; block < 3; ++block) {
        for (unsigned n = 0; n < 32; ++n)
            for (unsigned c = 0; c < 16; ++c) input[n*16+c] = (float)(10000*c+32*block+n+1);
        if (block == 2) state.delays_i32[0] = 4;
        CHECK(api->process(&state, &context) == ORPHEUS_OK && out.frame_count == 32);
    }
    CHECK(output[0] == 63 && output[16] == 64 && output[32] == 10062 && output[48] == 10063);
    float saved[600];
    memcpy(saved, state.buffer, sizeof(saved));
    uint32_t frozen_cursor = state.cursor;
    OrpheusValue enabled = {.type = ORPHEUS_VALUE_BOOL, .value.b = false}, got;
    CHECK(api->set_parameter(&state, "enabled", &enabled) == ORPHEUS_OK);
    CHECK(api->get_parameter(&state, "enabled", &got) == ORPHEUS_OK && !got.value.b);
    for (unsigned i = 0; i < 32*16; ++i) input[i] = -(float)(i+1);
    CHECK(api->process(&state, &context) == ORPHEUS_OK && !memcmp(output, input, sizeof(input)) &&
          state.cursor == frozen_cursor && !memcmp(saved, state.buffer, sizeof(saved)));
    out.frame_capacity = 31;
    CHECK(api->process(&state, &context) == ORPHEUS_ERR_INVALID_ARG && state.cursor == frozen_cursor &&
          !memcmp(saved, state.buffer, sizeof(saved)));
    out.frame_capacity = 32;
    out.data = input+1;
    CHECK(api->process(&state, &context) == ORPHEUS_ERR_INVALID_ARG);
    out.data = output;
    enabled.value.b = true;
    CHECK(api->set_parameter(&state, "enabled", &enabled) == ORPHEUS_OK);
    CHECK(api->process(&state, &context) == ORPHEUS_OK &&
          output[0] == saved[(frozen_cursor+1)%600] && output[1] == saved[(frozen_cursor+5)%600]);
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
    enabled.type = ORPHEUS_VALUE_INT;
    enabled.value.i32 = 0;
    CHECK(api->set_parameter(&state, "enabled", &enabled) == ORPHEUS_ERR_INVALID_ARG && state.enabled);
    const char* bad_key[] = {"enabled"};
    OrpheusConfig bad_config = {.param_ids = bad_key, .param_values = &enabled, .param_count = 1};
    CHECK(api->prepare(&state, &bad_config) == ORPHEUS_ERR_INVALID_ARG && state.enabled && state.buffer);
    enabled.type = ORPHEUS_VALUE_BOOL;
    enabled.value.b = false;
    CHECK(api->set_parameter(&state, "enabled", &enabled) == ORPHEUS_OK &&
          api->reset(&state) == ORPHEUS_OK && !state.enabled);
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
