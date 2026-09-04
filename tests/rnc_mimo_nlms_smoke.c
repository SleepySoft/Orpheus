#include "orpheus_rnc_mimo_nlms.h"

#include <math.h>
#include <string.h>

static int close_to(float actual, float expected, float tolerance) {
    return fabsf(actual - expected) <= tolerance;
}

static int run_static_weights(const OrpheusComponentInterface* interface) {
    RncMimoNlmsState state;
    const char* ids[] = {
        "reference_channels", "output_channels", "filter_length", "step_sizes",
        "leakage", "eps", "initial_weights"
    };
    OrpheusValue values[7] = {
        {.type=ORPHEUS_VALUE_INT, .value.i32=2},
        {.type=ORPHEUS_VALUE_INT, .value.i32=1},
        {.type=ORPHEUS_VALUE_INT, .value.i32=2},
        {.type=ORPHEUS_VALUE_STRING, .value.str="0"},
        {.type=ORPHEUS_VALUE_FLOAT, .value.f32=1.0f},
        {.type=ORPHEUS_VALUE_FLOAT, .value.f32=1.0e-5f},
        {.type=ORPHEUS_VALUE_STRING, .value.str="1,2,3,4"},
    };
    OrpheusConfig config = {
        .sample_rate=48000, .block_size=3, .channels=1,
        .param_ids=ids, .param_values=values, .param_count=7, .state_block=&state,
    };
    float refs[] = {1,10, 2,20, 3,30};
    float errors[] = {0,0,0};
    float output[3] = {0};
    OrpheusBuffer ref_buffer = {refs, ORPHEUS_FORMAT_F32, 2, 3, 3, true};
    OrpheusBuffer error_buffer = {errors, ORPHEUS_FORMAT_F32, 1, 3, 3, true};
    OrpheusBuffer output_buffer = {output, ORPHEUS_FORMAT_F32, 1, 3, 0, true};
    const OrpheusBuffer* inputs[] = {&ref_buffer, &error_buffer};
    OrpheusBuffer* outputs[] = {&output_buffer};
    OrpheusProcessContext context = {
        .state=&state, .inputs=inputs, .input_count=2, .outputs=outputs,
        .output_count=1, .frame_count=3, .sample_rate=48000,
    };
    void* state_ptr = NULL;
    if (interface->create(&state_ptr, &config) != ORPHEUS_OK || state_ptr != &state) return 10;
    if (interface->prepare(state_ptr, &config) != ORPHEUS_OK) return 11;
    if (interface->process(state_ptr, &context) != ORPHEUS_OK) return 12;
    if (!close_to(output[0], 31.0f, 1.0e-5f)) return 13;
    if (!close_to(output[1], 104.0f, 1.0e-5f)) return 14;
    if (!close_to(output[2], 177.0f, 1.0e-5f)) return 15;
    state.weights[0] = 99.0f;
    if (interface->reset(state_ptr) != ORPHEUS_OK) return 16;
    if (!close_to(state.weights[0], 1.0f, 1.0e-6f)) return 17;
    return 0;
}

static int run_adaptation(const OrpheusComponentInterface* interface) {
    RncMimoNlmsState state;
    const char* ids[] = {
        "reference_channels", "output_channels", "filter_length", "step_sizes", "eps"
    };
    OrpheusValue values[5] = {
        {.type=ORPHEUS_VALUE_INT, .value.i32=1},
        {.type=ORPHEUS_VALUE_INT, .value.i32=1},
        {.type=ORPHEUS_VALUE_INT, .value.i32=2},
        {.type=ORPHEUS_VALUE_STRING, .value.str="0.5"},
        {.type=ORPHEUS_VALUE_FLOAT, .value.f32=1.0e-5f},
    };
    OrpheusConfig config = {
        .sample_rate=48000, .block_size=2, .channels=1,
        .param_ids=ids, .param_values=values, .param_count=5, .state_block=&state,
    };
    float refs[] = {1,1};
    float errors[] = {1,1};
    float output[2] = {0};
    OrpheusBuffer ref_buffer = {refs, ORPHEUS_FORMAT_F32, 1, 2, 2, true};
    OrpheusBuffer error_buffer = {errors, ORPHEUS_FORMAT_F32, 1, 2, 2, true};
    OrpheusBuffer output_buffer = {output, ORPHEUS_FORMAT_F32, 1, 2, 0, true};
    const OrpheusBuffer* inputs[] = {&ref_buffer, &error_buffer};
    OrpheusBuffer* outputs[] = {&output_buffer};
    OrpheusProcessContext context = {
        .state=&state, .inputs=inputs, .input_count=2, .outputs=outputs,
        .output_count=1, .frame_count=2, .sample_rate=48000,
    };
    void* state_ptr = NULL;
    if (interface->create(&state_ptr, &config) != ORPHEUS_OK) return 20;
    if (interface->prepare(state_ptr, &config) != ORPHEUS_OK) return 21;
    if (interface->process(state_ptr, &context) != ORPHEUS_OK) return 22;
    if (!close_to(output[0], 0.0f, 1.0e-6f)) return 23;
    if (!close_to(output[1], 0.499995f, 1.0e-5f)) return 24;
    if (!close_to(state.weights[0], 0.749994f, 1.0e-5f)) return 25;
    if (!close_to(state.weights[1], 0.249999f, 1.0e-5f)) return 26;
    return 0;
}

int main(void) {
    const OrpheusComponentInterface* interface = orpheus_get_interface();
    int result;
    if (interface == NULL) return 1;
    result = run_static_weights(interface);
    if (result != 0) return result;
    return run_adaptation(interface);
}
