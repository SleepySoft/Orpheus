#include "component_state.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); return 1; } } while (0)
typedef orpheus_builtin_sleeping_beauty_State State;
typedef struct { State* state; unsigned calls,remaining; int error; } Delivery;
static int emit(void* user,uint32_t port,const OrpheusDelivery* delivery) {
    Delivery* result=user;
    if (port!=1 || !delivery || !delivery->buffer || delivery->frame_index!=333 ||
        delivery->epoch!=8 || delivery->buffer->frame_count!=16 ||
        result->state->algorithm.rampers[1].remaining!=result->remaining)
        return ORPHEUS_ERR_INVALID_ARG;
    ++result->calls;return result->error;
}

int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    State state,snapshot;
    const char* ids[]={"chan_map","ramp_ms"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_STRING,.value.str="0,1,1,2,3,-1"},
        {.type=ORPHEUS_VALUE_FLOAT,.value.f32=1}};
    OrpheusConfig config={.sample_rate=48000,.block_size=16,.channels=6,
        .param_ids=ids,.param_values=params,.param_count=2,.state_block=&state};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && handle==&state && api->prepare(handle,&config)==0);
    OrpheusValue index={.type=ORPHEUS_VALUE_FLOAT,.value.f32=10};
    CHECK(api->set_parameter(handle,"gain_index",&index)==0);
    float input[97],output[96];
    for (unsigned i=0;i<97;++i) input[i]=1;
    OrpheusBuffer in={.data=input,.channels=6,.format=ORPHEUS_FORMAT_F32,.interleaved=true,
        .frame_count=16,.frame_capacity=16},out=in;
    out.data=output;
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&out};
    Delivery result={.state=&state,.remaining=2,.error=-77};
    OrpheusContinuation continuation={.user=&result,.emit=emit};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,
        .frame_count=0,.frame_index=333,.epoch=8,.continuation=&continuation};
    snapshot=state;inputs[0]=NULL;outputs[0]=NULL;
    CHECK(api->process(handle,&ctx)==0 && result.calls==0 && !memcmp(&snapshot,&state,sizeof(state)));
    inputs[0]=&in;outputs[0]=&out;ctx.frame_count=16;
    CHECK(api->process(handle,&ctx)==-77 && result.calls==1 && !state.algorithm.controls_dirty);
    float expected=1,coefficient=state.algorithm.rampers[1].coefficient;
    for (unsigned n=0;n<16;++n) {
        expected*=coefficient;
        CHECK(output[n*6]==1 && output[n*6+1]==expected && output[n*6+2]==expected &&
            output[n*6+5]==0);
    }
    OrpheusValue freeze={.type=ORPHEUS_VALUE_BOOL,.value.b=true};
    CHECK(api->set_parameter(handle,"freeze",&freeze)==0);
    result.error=0;
    CHECK(api->process(handle,&ctx)==0 && result.calls==2);
    for (unsigned n=0;n<16;++n) CHECK(output[n*6+1]==expected && output[n*6+2]==expected);
    freeze.value.b=false;
    CHECK(api->set_parameter(handle,"freeze",&freeze)==0);
    for (unsigned remaining=1;;--remaining) {
        result.remaining=remaining;
        CHECK(api->process(handle,&ctx)==0);
        if (!remaining) break;
    }
    CHECK(result.calls==4 && state.algorithm.rampers[1].coefficient==1 &&
        state.algorithm.rampers[1].current_gain==state.algorithm.rampers[1].target_gain);
    snapshot=state;in.frame_count=15;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    in.frame_count=16;out.data=input+1;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    out.data=&state;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    out.data=output;in.data=state.algorithm.rampers;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    in.data=input;outputs[0]=NULL;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    outputs[0]=&out;inputs[0]=NULL;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    inputs[0]=&in;
    params[0].value.str="0,1,4";
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&snapshot,&state,sizeof(state)));
    params[0].value.str="0,1,1,2,3,-1";
    OrpheusValue bad={.type=ORPHEUS_VALUE_FLOAT,.value.f32=NAN};
    CHECK(api->set_parameter(handle,"gain_index",&bad)==ORPHEUS_ERR_INVALID_ARG &&
        !memcmp(&snapshot,&state,sizeof(state)));
    CHECK(api->reset(handle)==0 && state.gain_index==state.offset);
    for (unsigned r=0;r<4;++r) CHECK(state.algorithm.rampers[r].current_gain==1 &&
        state.algorithm.rampers[r].remaining==0);
    result.remaining=0;out.data=input;
    CHECK(api->process(handle,&ctx)==0 && result.calls==5 && input[0]==1 && input[5]==0);
    return api->destroy(handle);
}
