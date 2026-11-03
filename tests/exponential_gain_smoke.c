#include "orpheus_exponential_gain.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); return 1; } } while (0)

int main(void) {
    const OrpheusComponentInterface* api = orpheus_get_interface();
    ExponentialGainState state = {0};
    OrpheusConfig config = {.channels=64, .block_size=3};
    CHECK(api->prepare(&state, &config) == ORPHEUS_OK);
    float data[192];
    for (unsigned i = 0; i < 192; ++i) data[i] = (float)((int)i-97)/128;
    OrpheusBuffer buffer = {.data=data, .channels=64, .frame_capacity=3, .frame_count=3};
    const OrpheusBuffer* inputs[] = {&buffer};
    OrpheusBuffer* outputs[] = {&buffer};
    OrpheusProcessContext ctx = {.inputs=inputs, .outputs=outputs, .input_count=1, .output_count=1,
        .frame_count=3};
    const char* keys[] = {"target_gain", "ramp_coefficient", "remaining_blocks"};
    float request[] = {0.25f, 0.5f, 2.0f};
    for (unsigned k = 0; k < 3; ++k) {
        OrpheusValue value = {.type=ORPHEUS_VALUE_FLOAT, .value.f32=request[k]};
        CHECK(api->set_parameter(&state, keys[k], &value) == ORPHEUS_OK);
    }
    CHECK(api->process(&state, &ctx) == ORPHEUS_OK);
    for (unsigned n = 0; n < 3; ++n)
        for (unsigned c = 0; c < 64; ++c)
            CHECK(data[n*64+c] == ((float)((int)(n*64+c)-97)/128)*ldexpf(1.0f,-(int)n-1));
    CHECK(state.current_gain == 0.125f && state.filter_state == 0.0625f && state.remaining_blocks == 1.0f);
    CHECK(api->process(&state, &ctx) == ORPHEUS_OK);
    CHECK(state.current_gain == 0.015625f && state.remaining_blocks == 0.0f);
    CHECK(api->process(&state, &ctx) == ORPHEUS_OK && state.current_gain == 0.25f);
    CHECK(state.delayed_target == 0.25f && state.filter_state == 0.0078125f);
    CHECK(api->reset(&state) == ORPHEUS_OK && state.load_initial && state.current_gain == 1.0f);
    CHECK(state.target_gain == 0.25f && state.ramp_coefficient == 0.5f && state.remaining_blocks == 0.0f);
    ctx.frame_count = 0;
    CHECK(api->process(&state, &ctx) == ORPHEUS_OK && state.current_gain == 1.0f);
    ctx.frame_count = 3;
    inputs[0] = NULL;
    CHECK(api->process(&state, &ctx) == ORPHEUS_OK);
    for (unsigned i = 0; i < 192; ++i) CHECK(data[i] == 0.0f && !signbit(data[i]));
    outputs[0] = NULL;
    CHECK(api->process(&state, &ctx) == ORPHEUS_OK);
    OrpheusValue bad = {.type=ORPHEUS_VALUE_FLOAT, .value.f32=NAN};
    for (unsigned k = 0; k < 3; ++k) CHECK(api->set_parameter(&state, keys[k], &bad) != ORPHEUS_OK);
    bad.value.f32 = 0.5f;
    CHECK(api->set_parameter(&state, "remaining_blocks", &bad) != ORPHEUS_OK);
    bad.value.f32 = 16777218.0f;
    CHECK(api->set_parameter(&state, "remaining_blocks", &bad) != ORPHEUS_OK);
    state.target_gain = NAN;
    ExponentialGainState before = state;
    CHECK(api->process(&state, &ctx) == ORPHEUS_ERR_INVALID_ARG && !memcmp(&before, &state, sizeof(state)));
    config.channels = 65;
    CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    config.channels = 1;
    const char* id = "min_gain";
    bad.value.f32 = 0.0f;
    config.param_ids = &id; config.param_values = &bad; config.param_count = 1;
    CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    config.param_count = 0;
    CHECK(api->prepare(&state, &config) == ORPHEUS_OK);
    state.target_gain = state.current_gain + 0.000001f;
    state.snap_threshold = 0.001f;
    state.remaining_blocks = 2.0f;
    state.filter_state = 0.125f;
    CHECK(api->process(&state, &ctx) == ORPHEUS_OK && state.current_gain == state.target_gain);
    CHECK(state.load_initial && state.filter_state == 0.125f && state.remaining_blocks == 2.0f);
    void* allocated = NULL;
    CHECK(api->create(&allocated, &config) == ORPHEUS_OK && allocated != NULL);
    CHECK(api->prepare(allocated, &config) == ORPHEUS_OK && api->destroy(allocated) == ORPHEUS_OK);
    config.state_block = &state;
    CHECK(api->create(&allocated, &config) == ORPHEUS_OK && allocated == &state);
    CHECK(api->prepare(allocated, &config) == ORPHEUS_OK && api->destroy(allocated) == ORPHEUS_OK);
    return 0;
}
