#include "component_state.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); return 1; } } while (0)
typedef orpheus_builtin_input_mixer_3d_State State;
static OrpheusSlotInfo weights;
static bool fail_registration;
static OrpheusSlotId capture(void* user,const OrpheusSlotInfo* slot) {
    (void)user;
    if (slot->kind==ORPHEUS_SLOT_BULK) weights=*slot;
    return fail_registration ? ORPHEUS_SLOT_ID_INVALID : 0;
}
typedef struct { unsigned calls; int error; } Delivery;
static int emit(void* user,uint32_t port,const OrpheusDelivery* delivery) {
    Delivery* result=user;
    if (port!=1 || !delivery || !delivery->buffer || delivery->frame_index!=333 ||
        delivery->epoch!=8 || delivery->buffer->frame_count!=1) return ORPHEUS_ERR_INVALID_ARG;
    ++result->calls;return result->error;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    static State state,snapshot;
    const char* ids[]={"input_channels","output_channels","weights"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=3},
        {.type=ORPHEUS_VALUE_INT,.value.i32=2},
        {.type=ORPHEUS_VALUE_STRING,.value.str="1,1,1,.5,-.5,2"}};
    OrpheusConfig config={.param_ids=ids,.param_values=params,.param_count=3,.state_block=&state};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && handle==&state && api->prepare(handle,&config)==0);
    OrpheusRegistry registry={.add=capture};
    CHECK(api->register_slots(handle,&registry)==0 && weights.count==1024 &&
        weights.flags&ORPHEUS_SLOT_DOUBLE_BUFFERED && weights.type==ORPHEUS_VALUE_FLOAT &&
        (float*)((char*)&state+weights.offset)==state.algorithm.weights);
    fail_registration=true;
    CHECK(api->register_slots(handle,&registry)==ORPHEUS_ERR_PROCESSING);
    fail_registration=false;
    for (unsigned r=0;r<32;++r) for (unsigned c=0;c<32;++c)
        if (r>=2 || c>=3) state.algorithm.weights[r*32+c]=NAN;
    float input[]={1e20f,-1e20f,1,2},output[2];
    OrpheusBuffer in={.data=input,.channels=3,.format=ORPHEUS_FORMAT_F32,.interleaved=true,
        .frame_count=1,.frame_capacity=1},out=in;
    out.data=output;out.channels=2;
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&out};
    Delivery result={.error=-77};OrpheusContinuation continuation={.user=&result,.emit=emit};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,
        .frame_count=1,.frame_index=333,.epoch=8,.continuation=&continuation};
    CHECK(api->process(handle,&ctx)==-77 && result.calls==1 && output[0]==1);
    float expected=(.5f*input[0]-.5f*input[1])+2*input[2];
    CHECK(output[1]==expected);
    OrpheusValue gain={.type=ORPHEUS_VALUE_FLOAT,.value.f32=6};
    CHECK(api->set_parameter(handle,"gain_db",&gain)==0);
    result.error=0;
    CHECK(api->process(handle,&ctx)==0 && result.calls==2 && output[0]==powf(10.0f,.3f));
    state.gain_db=-6;
    CHECK(api->process(handle,&ctx)==0 && output[0]==powf(10.0f,-.3f));
    snapshot=state;in.frame_count=0;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    in.frame_count=1;
    void* aliases[]={input,input+1,&state,state.algorithm.weights};
    for (unsigned i=0;i<sizeof(aliases)/sizeof(aliases[0]);++i) {
        out.data=aliases[i];
        CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    }
    out.data=output;in.data=state.algorithm.weights;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    in.data=input;outputs[0]=NULL;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    outputs[0]=&out;inputs[0]=NULL;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    ctx.frame_count=0;outputs[0]=NULL;
    CHECK(api->process(handle,&ctx)==0 && result.calls==3 && !memcmp(&snapshot,&state,sizeof(state)));
    params[2].value.str="1,broken";
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&snapshot,&state,sizeof(state)));
    CHECK(api->reset(handle)==0 && !memcmp(&snapshot,&state,sizeof(state)));
    params[2].value.str="";
    CHECK(api->prepare(handle,&config)==0 && state.algorithm.weights[0]==1 &&
        state.algorithm.weights[33]==1 && state.algorithm.weights[32]==0);
    params[2].value.str="0";
    CHECK(api->prepare(handle,&config)==0 && state.algorithm.weights[0]==0 &&
        state.algorithm.weights[33]==1);
    return api->destroy(handle);
}
