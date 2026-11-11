#include "component_state.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); return 1; } } while (0)
typedef struct { unsigned calls; int error; } Delivery;
static int emit(void* user, uint32_t port, const OrpheusDelivery* delivery) {
    Delivery* result=user;
    if (port!=1 || !delivery || !delivery->buffer || delivery->frame_index!=333 ||
        delivery->epoch!=8 || delivery->buffer->frame_count!=4) return ORPHEUS_ERR_INVALID_ARG;
    ++result->calls;
    return result->error;
}
static unsigned bulk_slots;
static bool fail_registration;
static OrpheusSlotId capture(void* user,const OrpheusSlotInfo* slot) {
    (void)user;
    if (slot->kind==ORPHEUS_SLOT_BULK) {
        if (slot->count!=4 || !(slot->flags&ORPHEUS_SLOT_DOUBLE_BUFFERED))
            return ORPHEUS_SLOT_ID_INVALID;
        ++bulk_slots;
    }
    return fail_registration ? ORPHEUS_SLOT_ID_INVALID : 0;
}
int main(void) {
    const OrpheusComponentInterface* iface=orpheus_get_interface();
    const char* ids[]={"execution_mode","rows","cols","ramp_coeff","requested_gains"};
    OrpheusValue params[]={
        {.type=ORPHEUS_VALUE_INT,.value.i32=1}, {.type=ORPHEUS_VALUE_INT,.value.i32=2},
        {.type=ORPHEUS_VALUE_INT,.value.i32=2}, {.type=ORPHEUS_VALUE_FLOAT,.value.f32=.999f},
        {.type=ORPHEUS_VALUE_STRING,.value.str="0,-1,.3,1"}
    };
    OrpheusConfig config={.sample_rate=48000,.block_size=4,.channels=2,.param_count=5,.param_ids=ids,.param_values=params};
    static orpheus_builtin_slc_matrix_mul_State state,snapshot;
    config.state_block=&state;void* handle=NULL;
    CHECK(iface->create(&handle,&config)==0 && handle==&state);
    CHECK(iface->prepare(&state,&config)==ORPHEUS_OK && !state.initialized);
    OrpheusRegistry registry={.add=capture};
    CHECK(iface->register_slots(&state,&registry)==0 && bulk_slots==3);
    fail_registration=true;
    CHECK(iface->register_slots(&state,&registry)==ORPHEUS_ERR_PROCESSING);
    fail_registration=false;
    float input[]={1,.5f,-1,.25f,.125f,-.5f,0,1},output[8],expected[8];
    OrpheusBuffer in={.data=input,.channels=2,.frame_count=4,.frame_capacity=4,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer out={.data=output,.channels=2,.frame_capacity=4,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    const OrpheusBuffer* inputs[]={&in}; OrpheusBuffer* outputs[]={&out};
    Delivery result={0};OrpheusContinuation continuation={.user=&result,.emit=emit};
    OrpheusProcessContext context={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,.frame_count=0,
        .frame_index=333,.epoch=8,.continuation=&continuation};
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && !state.initialized);
    context.frame_count=4;
    result.error=-77;
    CHECK(iface->process(&state,&context)==-77 && state.initialized && result.calls==1);
    result.error=0;
    memcpy(expected,output,sizeof(output));
    float active[4]; memcpy(active,state.active_gains,sizeof(active));
    for (unsigned k=0;k<4;++k) state.algorithm.requested_gains[k]=.7f;
    CHECK(iface->process(&state,&context)==ORPHEUS_OK);
    CHECK(memcmp(state.accepted_targets,state.algorithm.requested_gains,sizeof(active))!=0);
    OrpheusValue sequence={.type=ORPHEUS_VALUE_INT,.value.i32=1};
    CHECK(iface->set_parameter(&state,"sequence",&sequence)==ORPHEUS_OK);
    memcpy(active,state.active_gains,sizeof(active));
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && !memcmp(active,state.active_gains,sizeof(active)));
    sequence.value.i32=INT32_MIN;
    CHECK(iface->set_parameter(&state,"sequence",&sequence)==ORPHEUS_OK);
    for (unsigned k=0;k<4;++k) active[k]=fmaxf(active[k],SLC_MM_SILENT_GAIN);
    for (unsigned n=0;n<4;++n) {
        for (unsigned k=0;k<4;++k) active[k]=(1-state.algorithm.active_coefficient)*.7f+state.algorithm.active_coefficient*active[k];
        for (unsigned r=0;r<2;++r) {
            float value=0;
            for (unsigned c=0;c<2;++c) value+=input[n*2+c]*active[r*2+c];
            expected[n*2+r]=value;
        }
    }
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && !memcmp(expected,output,sizeof(output)) &&
          !memcmp(active,state.active_gains,sizeof(active)) && state.last_sequence==INT32_MIN && result.calls==4);
    snapshot=state;
    in.frame_count=3;
    CHECK(iface->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG);
    in.frame_count=4;
    void* aliases[]={input,input+1,&state,state.active_gains,state.accepted_targets,state.algorithm.requested_gains};
    for (unsigned i=0;i<sizeof(aliases)/sizeof(aliases[0]);++i) {
        out.data=aliases[i];
        CHECK(iface->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG);
    }
    out.data=output;in.data=state.active_gains;
    CHECK(iface->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG);
    in.data=input;outputs[0]=NULL;
    CHECK(iface->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG);
    outputs[0]=&out;inputs[0]=NULL;
    CHECK(iface->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG);
    context.frame_count=0;outputs[0]=NULL;
    CHECK(iface->process(&state,&context)==0 && !memcmp(&snapshot,&state,sizeof(state)) && result.calls==4);
    inputs[0]=&in;outputs[0]=&out;context.frame_count=4;
    CHECK(iface->reset(&state)==ORPHEUS_OK && !state.initialized && !state.last_sequence);
    state.algorithm.requested_gains[0]=NAN;
    memcpy(expected,output,sizeof(output));
    CHECK(iface->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG && !memcmp(expected,output,sizeof(output)));
    snapshot=state;params[4].value.str="1,2";
    CHECK(iface->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(!memcmp(&snapshot,&state,sizeof(state)));
    params[4].value.str="1,2,3,4"; params[1].value.i32=33;
    CHECK(iface->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG);
    config.param_count=0;
    CHECK(iface->prepare(&state,&config)==ORPHEUS_OK);
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && !memcmp(input,output,sizeof(input)));
    const char* interpolation_ids[]={"tables","num_tables","interp_x","interp_index"};
    OrpheusValue interpolation_values[]={
        {.type=ORPHEUS_VALUE_STRING,.value.str="1,0,0,1,3,0,0,3"},
        {.type=ORPHEUS_VALUE_INT,.value.i32=2},
        {.type=ORPHEUS_VALUE_STRING,.value.str="0,1"},
        {.type=ORPHEUS_VALUE_FLOAT,.value.f32=0}};
    config.param_ids=interpolation_ids;config.param_values=interpolation_values;config.param_count=4;
    CHECK(iface->prepare(&state,&config)==0);
    state.interp_index=.5f;
    CHECK(iface->process(&state,&context)==0);
    for (unsigned i=0;i<8;++i) CHECK(output[i]==2*input[i]);
    OrpheusValue freeze={.type=ORPHEUS_VALUE_INT,.value.i32=1};
    CHECK(iface->set_parameter(&state,"freeze",&freeze)==0);
    state.interp_index=1;
    CHECK(iface->process(&state,&context)==0 && state.accepted_targets[0]==3);
    for (unsigned i=0;i<8;++i) CHECK(output[i]==2*input[i]);
    freeze.value.i32=0;
    CHECK(iface->set_parameter(&state,"freeze",&freeze)==0 && iface->reset(&state)==0);
    CHECK(iface->process(&state,&context)==0);
    for (unsigned i=0;i<8;++i) CHECK(output[i]==3*input[i]);
    snapshot=state;interpolation_values[2].value.str="1,0";
    CHECK(iface->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&snapshot,&state,sizeof(state)));
    interpolation_values[2].value.str="0,1,";
    CHECK(iface->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG);
    interpolation_values[2].value.str="0,1";interpolation_values[0].value.str="bad";
    CHECK(iface->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG);
    return iface->destroy(&state);
}
