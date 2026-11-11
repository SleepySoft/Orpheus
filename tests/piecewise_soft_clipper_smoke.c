#include "component_state.h"

#include <math.h>
#include <string.h>

static int close_to(float actual, float expected) {
    return fabsf(actual - expected) <= 1.0e-6f;
}

int main(void) {
    orpheus_builtin_piecewise_soft_clipper_State state;
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
    {
        OrpheusValue low = {.type=ORPHEUS_VALUE_INT, .value.i32=0};
        OrpheusValue bypass = {.type=ORPHEUS_VALUE_BOOL, .value.b=true};
        state.xmin_low = 2.0f;
        state.xmax_low = 4.0f;
        state.p2_low = 0.25f;
        input_data[0] = 3.0f;
        input_data[1] = -8.0f;
        input_data[2] = 2.0f;
        input_data[3] = -0.0f;
        if (interface->set_parameter(state_ptr, "param_set", &low) != ORPHEUS_OK) return 11;
        if (interface->process(state_ptr, &context) != ORPHEUS_OK) return 12;
        if (!close_to(output_data[0], 2.75f) || !close_to(output_data[1], -3.0f)) return 13;
        if (state.active_mask != 3 || !signbit(output_data[3])) return 14;
        if (interface->reset(state_ptr) != ORPHEUS_OK || state.active_mask != 0) return 15;
        if (state.param_set != 0 || state.p2_low != 0.25f) return 16;
        input_data[0] = NAN;
        input_data[1] = 3.0f;
        input_data[2] = 8.0f;
        input_data[3] = NAN;
        if (interface->process(state_ptr, &context) != ORPHEUS_OK || state.active_mask != 2) return 17;
        if (interface->set_parameter(state_ptr, "disabled", &bypass) != ORPHEUS_OK) return 18;
        if (interface->process(state_ptr, &context) != ORPHEUS_OK) return 19;
        if (memcmp(input_data, output_data, sizeof(input_data)) != 0 || state.active_mask != 0) return 20;
        if (interface->reset(state_ptr) != ORPHEUS_OK || !state.disabled) return 21;
        if (interface->process(state_ptr, &context) != ORPHEUS_OK) return 22;
        if (memcmp(input_data, output_data, sizeof(input_data)) != 0) return 23;
    }
    OrpheusValue read_only={.type=ORPHEUS_VALUE_INT,.value.i32=3};
    if(interface->set_parameter(state_ptr,"active_mask",&read_only)!=ORPHEUS_ERR_UNSUPPORTED) return 24;
    input.frame_count=1;
    if(interface->process(state_ptr,&context)!=ORPHEUS_ERR_INVALID_ARG) return 25;
    input.frame_count=2;output.data=input_data+1;
    if(interface->process(state_ptr,&context)!=ORPHEUS_ERR_INVALID_ARG) return 26;
    output.data=input_data;
    if(interface->process(state_ptr,&context)!=ORPHEUS_OK) return 27;
    if(interface->destroy(state_ptr)!=ORPHEUS_OK) return 28;
    return 0;
}
