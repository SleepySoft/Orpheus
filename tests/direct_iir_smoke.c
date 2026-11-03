#include "orpheus_iir.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%d: %s\n", __LINE__, #expr); return 1; } } while (0)

int main(void) {
    const OrpheusComponentInterface* api = orpheus_get_interface();
    IirState state = {0};
    const char* keys[] = {"order", "coefs"};
    OrpheusValue values[] = {
        {.type = ORPHEUS_VALUE_INT, .value.i32 = 1},
        {.type = ORPHEUS_VALUE_STRING, .value.str = "0.5,0.25,-0.5"}
    };
    OrpheusConfig config = {.channels = 2, .param_count = 2, .param_ids = keys, .param_values = values};
    CHECK(api->prepare(&state, &config) == ORPHEUS_OK);
    float input[4] = {1, 2, 1, 2}, output[4];
    OrpheusBuffer in = {.data = input}, out = {.data = output};
    const OrpheusBuffer* inputs[] = {&in};
    OrpheusBuffer* outputs[] = {&out};
    OrpheusProcessContext context = {.inputs = inputs, .outputs = outputs, .frame_count = 1};
    CHECK(api->process(&state, &context) == ORPHEUS_OK && output[0] == 0.5f && output[1] == 1);
    memset(state.coefs[0], 0, sizeof(state.coefs[0]));
    context.frame_count = 2;
    CHECK(api->process(&state, &context) == ORPHEUS_OK);
    CHECK(output[0] == 0.5f && output[1] == 1 && output[2] == 0 && output[3] == 0);
    CHECK(api->prepare(&state, &config) == ORPHEUS_OK);
    context.frame_count = 1;
    CHECK(api->process(&state, &context) == ORPHEUS_OK);
    float frozen[IIR_MAX_CHANNELS][IIR_MAX_ORDER];
    memcpy(frozen, state.history, sizeof(frozen));
    OrpheusValue enabled = {.type = ORPHEUS_VALUE_INT, .value.i32 = 0};
    CHECK(api->set_parameter(&state, "processing_enabled", &enabled) == ORPHEUS_OK);
    inputs[0] = NULL;
    CHECK(api->process(&state, &context) == ORPHEUS_OK);
    CHECK(output[0] == 0 && !signbit(output[0]) && output[1] == 0 && !signbit(output[1]));
    CHECK(memcmp(frozen, state.history, sizeof(frozen)) == 0);
    CHECK(api->reset(&state) == ORPHEUS_OK && state.processing_enabled == 0 && state.coefs[0][0] == 0.5f);
    CHECK(state.history[0][0] == 0);
    const char* invalid[] = {"", "1,0", "1,0,0,1", "1,0,0,", "1,nan,0", "1,inf,0", "1,0,0suffix"};
    for (unsigned n = 0; n < sizeof(invalid)/sizeof(invalid[0]); ++n) {
        values[1].value.str = invalid[n];
        CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    }
    char coefs[256] = "1";
    for (unsigned n = 0; n < 32; ++n) strcat(coefs, ",0");
    values[0].value.i32 = 16; values[1].value.str = coefs; config.channels = 64;
    CHECK(api->prepare(&state, &config) == ORPHEUS_OK);
    float wide_in[64] = {0}, wide_out[64];
    wide_in[63] = 1; in.data = wide_in; out.data = wide_out; inputs[0] = &in;
    CHECK(api->process(&state, &context) == ORPHEUS_OK && wide_out[63] == 1);
    for (unsigned n = 0; n < 63; ++n) CHECK(wide_out[n] == 0);
    values[0].value.i32 = 17;
    CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    values[0].value.i32 = 16; config.channels = 65;
    CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    return 0;
}
