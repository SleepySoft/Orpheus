#include "orpheus_iir_bank.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%d: %s\n", __LINE__, #expr); return 1; } } while (0)

int main(void) {
    const OrpheusComponentInterface* api = orpheus_get_interface();
    IirBankState state = {0};
    const char* keys[] = {"num_stages", "coefs"};
    OrpheusValue values[] = {
        {.type = ORPHEUS_VALUE_INT, .value.i32 = 1},
        {.type = ORPHEUS_VALUE_STRING, .value.str = "0.5,0.25,0,-0.5,0"}
    };
    OrpheusConfig config = {.channels = 2, .sample_rate = 48000, .block_size = 32,
        .param_count = 2, .param_ids = keys, .param_values = values};
    CHECK(api->prepare(&state, &config) == ORPHEUS_OK && state.processing_enabled == 1);
    float input[14] = {1, 2, 0, 0, 0, 0}, output[14];
    OrpheusBuffer in = {.data = input}, out = {.data = output};
    const OrpheusBuffer* inputs[] = {&in};
    OrpheusBuffer* outputs[] = {&out};
    OrpheusProcessContext context = {.inputs = inputs, .outputs = outputs, .frame_count = 3};
    CHECK(api->process(&state, &context) == ORPHEUS_OK);
    IirChannelState frozen[IIR_BANK_MAX_CHANNELS];
    memcpy(frozen, state.channelStates, sizeof(frozen));
    OrpheusValue enabled = {.type = ORPHEUS_VALUE_INT, .value.i32 = 0};
    CHECK(api->set_parameter(&state, "processing_enabled", &enabled) == ORPHEUS_OK);
    context.frame_count = 7;
    for (unsigned n = 0; n < 14; ++n) input[n] = n % 2 ? INFINITY : NAN;
    CHECK(api->process(&state, &context) == ORPHEUS_OK && out.frame_count == 7);
    for (unsigned n = 0; n < 14; ++n) CHECK(output[n] == 0 && !signbit(output[n]));
    CHECK(memcmp(frozen, state.channelStates, sizeof(frozen)) == 0);
    inputs[0] = NULL;
    CHECK(api->process(&state, &context) == ORPHEUS_OK);
    enabled.value.i32 = 2;
    CHECK(api->set_parameter(&state, "processing_enabled", &enabled) == ORPHEUS_ERR_INVALID_ARG);
    enabled.type = ORPHEUS_VALUE_FLOAT;
    CHECK(api->set_parameter(&state, "processing_enabled", &enabled) == ORPHEUS_ERR_INVALID_ARG);
    CHECK(state.processing_enabled == 0);
    enabled.type = ORPHEUS_VALUE_INT; enabled.value.i32 = 1;
    CHECK(api->set_parameter(&state, "processing_enabled", &enabled) == ORPHEUS_OK);
    CHECK(api->process(&state, &context) == ORPHEUS_ERR_INVALID_ARG);
    inputs[0] = &in;
    memset(input, 0, sizeof(input));
    context.frame_count = 1;
    CHECK(api->process(&state, &context) == ORPHEUS_OK);
    CHECK(output[0] == frozen[0].z1[0] && output[1] == frozen[1].z1[0]);
    enabled.value.i32 = 0;
    CHECK(api->set_parameter(&state, "processing_enabled", &enabled) == ORPHEUS_OK);
    CHECK(api->reset(&state) == ORPHEUS_OK && state.processing_enabled == 0);
    memset(frozen, 0, sizeof(frozen));
    CHECK(memcmp(frozen, state.channelStates, sizeof(frozen)) == 0 && state.coefs[0][0] == 0.5f);
    const char* disabled_keys[] = {"processing_enabled"};
    OrpheusValue disabled = {.type = ORPHEUS_VALUE_INT, .value.i32 = 0};
    config.param_ids = disabled_keys; config.param_values = &disabled; config.param_count = 1;
    CHECK(api->prepare(&state, &config) == ORPHEUS_OK && state.processing_enabled == 0);
    disabled.value.i32 = -1;
    CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    return 0;
}
