#include "orpheus_slc_matrix_mul.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); return 1; } } while (0)
int main(void) {
    const OrpheusComponentInterface* iface=orpheus_get_interface();
    const char* ids[]={"execution_mode","rows","cols","ramp_coeff","requested_gains"};
    OrpheusValue params[]={
        {.type=ORPHEUS_VALUE_INT,.value.i32=1}, {.type=ORPHEUS_VALUE_INT,.value.i32=2},
        {.type=ORPHEUS_VALUE_INT,.value.i32=2}, {.type=ORPHEUS_VALUE_FLOAT,.value.f32=.999f},
        {.type=ORPHEUS_VALUE_STRING,.value.str="0,-1,.3,1"}
    };
    OrpheusConfig config={.sample_rate=48000,.block_size=4,.channels=2,.param_count=5,.param_ids=ids,.param_values=params};
    SlcMatrixMulState state={0};
    CHECK(iface->prepare(&state,&config)==ORPHEUS_OK && !state.initialized);
    float input[]={1,.5f,-1,.25f,.125f,-.5f,0,1},output[8],expected[8];
    OrpheusBuffer in={.data=input,.channels=2,.frame_capacity=4,.format=ORPHEUS_FORMAT_F32};
    OrpheusBuffer out={.data=output,.channels=2,.frame_capacity=4,.format=ORPHEUS_FORMAT_F32};
    const OrpheusBuffer* inputs[]={&in}; OrpheusBuffer* outputs[]={&out};
    OrpheusProcessContext context={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,.frame_count=0};
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && !state.initialized);
    context.frame_count=4;
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && state.initialized);
    memcpy(expected,output,sizeof(output));
    float active[4]; memcpy(active,state.active,sizeof(active));
    for (unsigned k=0;k<4;++k) state.requested_gains[k]=.7f;
    CHECK(iface->process(&state,&context)==ORPHEUS_OK);
    CHECK(memcmp(state.target,state.requested_gains,sizeof(active))!=0);
    OrpheusValue sequence={.type=ORPHEUS_VALUE_INT,.value.i32=1};
    CHECK(iface->set_parameter(&state,"sequence",&sequence)==ORPHEUS_OK);
    memcpy(active,state.active,sizeof(active));
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && !memcmp(active,state.active,sizeof(active)));
    sequence.value.i32=INT32_MIN;
    CHECK(iface->set_parameter(&state,"sequence",&sequence)==ORPHEUS_OK);
    for (unsigned k=0;k<4;++k) active[k]=fmaxf(active[k],SLC_MM_SILENT_GAIN);
    for (unsigned n=0;n<4;++n) {
        for (unsigned k=0;k<4;++k) active[k]=(1-state.activeCoeff)*.7f+state.activeCoeff*active[k];
        for (unsigned r=0;r<2;++r) {
            float value=0;
            for (unsigned c=0;c<2;++c) value+=input[n*2+c]*active[r*2+c];
            expected[n*2+r]=value;
        }
    }
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && !memcmp(expected,output,sizeof(output)) &&
          !memcmp(active,state.active,sizeof(active)) && state.last_sequence==INT32_MIN);
    CHECK(iface->reset(&state)==ORPHEUS_OK && !state.initialized && !state.last_sequence);
    state.requested_gains[0]=NAN;
    memcpy(expected,output,sizeof(output));
    CHECK(iface->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG && !memcmp(expected,output,sizeof(output)));
    params[4].value.str="1,2";
    CHECK(iface->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG);
    params[4].value.str="1,2,3,4"; params[1].value.i32=33;
    CHECK(iface->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG);
    config.param_count=0;
    CHECK(iface->prepare(&state,&config)==ORPHEUS_OK);
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && !memcmp(input,output,sizeof(input)));
    return 0;
}
