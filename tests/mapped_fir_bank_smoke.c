#include "orpheus_mapped_fir_bank.h"

#include <math.h>
#include <string.h>

static int close_to(float actual, float expected) {
    return fabsf(actual - expected) <= 1.0e-6f;
}

int main(void) {
    MappedFirBankState state;
    memset(&state, 0, sizeof(state));
    const char* ids[] = {
        "input_channels", "filter_count", "output_channels", "filter_lengths",
        "coefficient_mapping", "input_mapping", "output_starts",
        "output_delay_samples", "coefficients"
    };
    OrpheusValue values[] = {
        {.type=ORPHEUS_VALUE_INT, .value.i32=2},
        {.type=ORPHEUS_VALUE_INT, .value.i32=4},
        {.type=ORPHEUS_VALUE_INT, .value.i32=2},
        {.type=ORPHEUS_VALUE_STRING, .value.str="3,3,3,3"},
        {.type=ORPHEUS_VALUE_STRING, .value.str="0,1,2,3"},
        {.type=ORPHEUS_VALUE_STRING, .value.str="0,1,0,1"},
        {.type=ORPHEUS_VALUE_STRING, .value.str="0,2"},
        {.type=ORPHEUS_VALUE_INT, .value.i32=2},
        {.type=ORPHEUS_VALUE_STRING, .value.str="0,0,1,0,0,1,0,0,0.5,0,0,0.5"},
    };
    OrpheusConfig config = {
        .sample_rate=48000, .block_size=6, .channels=2,
        .param_ids=ids, .param_values=values, .param_count=9, .state_block=&state,
    };
    float input_data[] = {1,2, 3,4, 5,6, 7,8, 9,10, 11,12};
    float output_data[12] = {0};
    OrpheusBuffer input = {input_data, ORPHEUS_FORMAT_F32, 2, 48000, 6, true};
    OrpheusBuffer output = {output_data, ORPHEUS_FORMAT_F32, 2, 48000, 0, true};
    const OrpheusBuffer* inputs[] = {&input};
    OrpheusBuffer* outputs[] = {&output};
    OrpheusProcessContext context = {
        .state=&state, .inputs=inputs, .input_count=1, .outputs=outputs,
        .output_count=1, .frame_count=6, .sample_rate=48000,
    };
    const OrpheusComponentInterface* interface = orpheus_get_interface();
    void* state_ptr = NULL;
    if (!interface) return 1;
    if (interface->create(&state_ptr, &config) != ORPHEUS_OK) return 2;
    if (interface->prepare(state_ptr, &config) != ORPHEUS_OK) return 3;
    if (interface->process(state_ptr, &context) != ORPHEUS_OK) return 4;
    if (!close_to(output_data[0], 0.0f) || !close_to(output_data[1], 0.0f)) return 5;
    if (!close_to(output_data[2], 0.0f) || !close_to(output_data[3], 0.0f)) return 6;
    if (!close_to(output_data[4], 0.0f) || !close_to(output_data[5], 0.0f)) return 7;
    if (!close_to(output_data[6], 0.0f) || !close_to(output_data[7], 0.0f)) return 8;
    if (!close_to(output_data[8], 3.0f) || !close_to(output_data[9], 1.5f)) return 9;
    if (!close_to(output_data[10], 7.0f) || !close_to(output_data[11], 3.5f)) return 10;
    if (state.max_taps != 3 || state.coefficient_count != 12) return 11;
    if (interface->destroy(state_ptr) != ORPHEUS_OK) return 12;
    return 0;
}