#include "orpheus_baf_soft_clipper.h"

#include <math.h>

static int close_to(float actual, float expected) {
    return fabsf(actual - expected) <= 1.0e-6f;
}

int main(void) {
    BafSoftClipperState state;
    const char* ids[] = {"channels", "xmin", "xmax", "p2"};
    OrpheusValue values[] = {
        {.type=ORPHEUS_VALUE_INT, .value.i32=2},
        {.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.65f},
        {.type=ORPHEUS_VALUE_FLOAT, .value.f32=1.35f},
        {.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.714285731f},
    };
    OrpheusConfig config = {
        .sample_rate=48000, .block_size=2, .channels=2,
        .param_ids=ids, .param_values=values, .param_count=4, .state_block=&state,
    };
    float input_data[] = {0.5f, -1.0f, 2.0f, -0.65f};
    float output_data[4] = {0};
    OrpheusBuffer input = {input_data, ORPHEUS_FORMAT_F32, 2, 2, 2, true};
    OrpheusBuffer output = {output_data, ORPHEUS_FORMAT_F32, 2, 2, 0, true};
    const OrpheusBuffer* inputs[] = {&input};
    OrpheusBuffer* outputs[] = {&output};
    OrpheusProcessContext context = {
        .state=&state, .inputs=inputs, .input_count=1, .outputs=outputs,
        .output_count=1, .frame_count=2, .sample_rate=48000,
    };
    const OrpheusComponentInterface* interface = orpheus_get_interface();
    void* state_ptr = NULL;
    if (interface == NULL) return 1;
    if (interface->create(&state_ptr, &config) != ORPHEUS_OK) return 2;
    if (interface->prepare(state_ptr, &config) != ORPHEUS_OK) return 3;
    if (interface->process(state_ptr, &context) != ORPHEUS_OK) return 4;
    if (!close_to(output_data[0], 0.5f)) return 5;
    if (!close_to(output_data[1], -0.9125f)) return 6;
    if (!close_to(output_data[2], 1.0f)) return 7;
    if (!close_to(output_data[3], -0.65f)) return 8;
    if (state.active_mask != 3) return 9;
    {
        OrpheusValue invalid = {.type=ORPHEUS_VALUE_INT, .value.i32=2};
        if (interface->set_parameter(state_ptr, "param_set", &invalid) != ORPHEUS_ERR_INVALID_ARG) return 10;
    }
    return 0;
}
