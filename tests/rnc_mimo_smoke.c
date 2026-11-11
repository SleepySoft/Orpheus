#include "component_state.h"
#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if(!(expr)) { fprintf(stderr,"%d: %s\n",__LINE__,#expr);return 1; } } while(0)
typedef orpheus_builtin_rnc_mimo_nlms_State State;
typedef struct { unsigned calls;int error;OrpheusSlotInfo weights,steps;bool fail; } Capture;
static OrpheusSlotId add(void* user,const OrpheusSlotInfo* slot) {
    Capture* capture=user;
    if(!strcmp(slot->key,"weights")) capture->weights=*slot;
    if(!strcmp(slot->key,"step_sizes")) capture->steps=*slot;
    return capture->fail ? ORPHEUS_SLOT_ID_INVALID : 0;
}
static int emit(void* user,uint32_t port,const OrpheusDelivery* delivery) {
    Capture* capture=user;
    if(port!=1 || !delivery || !delivery->buffer) return ORPHEUS_ERR_INVALID_ARG;
    capture->calls++;
    return capture->error;
}

int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    static State state,saved;
    const char* ids[]={"reference_channels","output_channels","filter_length","step_sizes","initial_weights","leakage","eps"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=3},{.type=ORPHEUS_VALUE_INT,.value.i32=2},
        {.type=ORPHEUS_VALUE_INT,.value.i32=4},{.type=ORPHEUS_VALUE_STRING,.value.str=".125,.25"},
        {.type=ORPHEUS_VALUE_STRING,.value.str="1,.5,-.25"},{.type=ORPHEUS_VALUE_FLOAT,.value.f32=.75f},
        {.type=ORPHEUS_VALUE_FLOAT,.value.f32=.03125f}};
    OrpheusConfig config={.param_count=7,.param_ids=ids,.param_values=params,.state_block=&state};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0);
    Capture capture={0};
    OrpheusRegistry registry={.ctx=&capture,.add=add};
    CHECK(api->register_slots(handle,&registry)==0 &&
        capture.weights.count==24 && capture.weights.offset==offsetof(State,algorithm.weights) &&
        (capture.weights.flags & ORPHEUS_SLOT_DOUBLE_BUFFERED) &&
        capture.steps.count==2 && capture.steps.offset==offsetof(State,algorithm.step_sizes) &&
        !(capture.steps.flags & ORPHEUS_SLOT_DOUBLE_BUFFERED));
    capture.fail=true;CHECK(api->register_slots(handle,&registry)==ORPHEUS_ERR_PROCESSING);capture.fail=false;
    float reference[12],errors[8],output[8],weights[24]={1,.5f,-.25f},history[3][4]={{0}};
    OrpheusBuffer ref={.data=reference,.format=ORPHEUS_FORMAT_F32,.channels=3,.interleaved=true,
        .frame_count=3,.frame_capacity=4},err=ref,out=ref;
    err.data=errors;err.channels=2;out.data=output;out.channels=2;
    const OrpheusBuffer* inputs[]={&ref,&err};OrpheusBuffer* outputs[]={&out};
    OrpheusContinuation continuation={.user=&capture,.emit=emit};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=2,.output_count=1,
        .frame_count=3,.continuation=&continuation,.trigger_input=0};
    const unsigned lengths[]={3,1,2};
    unsigned sample_index=0;
    for(unsigned call=0;call<15;call++) {
        unsigned frames=lengths[call%3];
        ctx.frame_count=frames;ref.frame_count=frames;err.frame_count=frames;
        capture.error=call==0 ? -77 : 0;
        for(unsigned n=0;n<frames;n++) {
            for(unsigned r=0;r<3;r++) reference[n*3+r]=(float)((int)(((sample_index+n)*17+r*31)%103)-51)*.03125f;
            for(unsigned o=0;o<2;o++) errors[n*2+o]=(float)((int)(((sample_index+n)*13+o*7)%29)-14)*.0625f;
        }
        CHECK(api->process(handle,&ctx)==capture.error && capture.calls==call+1);
        unsigned start=((call%2)*3+(call/2)%3)*4;
        for(unsigned k=0;k<4;k++) weights[start+k]*=.75f;
        for(unsigned n=0;n<frames;n++) {
            float norm=.03125f;
            for(unsigned r=0;r<3;r++) {
                for(unsigned k=3;k>0;k--) history[r][k]=history[r][k-1];
                history[r][0]=reference[n*3+r];
                for(unsigned k=0;k<4;k++) norm+=history[r][k]*history[r][k];
            }
            for(unsigned o=0;o<2;o++) {
                float value=0,scale=(o==0 ? .125f : .25f)*errors[n*2+o]/norm;
                for(unsigned r=0;r<3;r++) for(unsigned k=0;k<4;k++) {
                    unsigned index=(o*3+r)*4+k;
                    value+=weights[index]*history[r][k];weights[index]+=scale*history[r][k];
                }
                CHECK(!memcmp(output+n*2+o,&value,sizeof(float)));
            }
        }
        sample_index+=frames;
        CHECK(!memcmp(weights,state.algorithm.weights,sizeof(weights)) &&
            state.algorithm.position==sample_index%4 && state.algorithm.leakage_output==(call+1)%2 &&
            state.algorithm.leakage_reference==((call+1)/2)%3);
        for(unsigned r=0;r<3;r++) for(unsigned k=0;k<4;k++)
            CHECK(history[r][k]==state.algorithm.history[r*4+(state.algorithm.position+3-k)%4]);
    }
    saved=state;
    const char* invalid[]={"nan","inf","1e500","1e-500",",1","1,","1;2","1,gap,2"};
    for(unsigned index=3;index<=4;index++) {
        OrpheusValue original=params[index];
        for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
            params[index].value.str=invalid[i];
            CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&saved,&state,sizeof(state)));
        }
        params[index]=original;
    }
    params[3].value.str="1,2,3";
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&saved,&state,sizeof(state)));
    params[3].value.str=".125,.25";
    params[2].value.i32=126;
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&saved,&state,sizeof(state)));
    params[2].value.i32=4;
    ctx.trigger_input=1;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);ctx.trigger_input=0;
    OrpheusBuffer* buffers[]={&ref,&err};
    void* samples[]={reference,errors};
    for(unsigned i=0;i<2;i++) {
        inputs[i]=NULL;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);inputs[i]=buffers[i];
        buffers[i]->frame_count=1;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);buffers[i]->frame_count=2;
        buffers[i]->data=state.algorithm.history;
        CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);buffers[i]->data=samples[i];
    }
    outputs[0]=NULL;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);outputs[0]=&out;
    out.frame_count=1;out.frame_capacity=1;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);out.frame_count=2;out.frame_capacity=4;
    void* aliases[]={reference,reference+1,errors,errors+1,&state,state.algorithm.weights,
        state.algorithm.step_sizes,state.algorithm.initial_weights,state.algorithm.history};
    for(unsigned i=0;i<sizeof(aliases)/sizeof(aliases[0]);i++) {
        out.data=aliases[i];CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    }
    out.data=output;
    uint32_t* cursors[]={&state.algorithm.position,&state.algorithm.leakage_output,&state.algorithm.leakage_reference};
    for(unsigned i=0;i<3;i++) {
        uint32_t previous=*cursors[i];*cursors[i]=UINT32_MAX;
        CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);*cursors[i]=previous;
    }
    CHECK(capture.calls==15 && !memcmp(&saved,&state,sizeof(state)));
    ctx.frame_count=0;inputs[0]=NULL;inputs[1]=NULL;outputs[0]=NULL;
    CHECK(api->process(handle,&ctx)==0 && capture.calls==15 && !memcmp(&saved,&state,sizeof(state)));
    state.algorithm.weights[0]=9;state.algorithm.step_sizes[0]=.5f;
    CHECK(api->reset(handle)==0 && state.algorithm.weights[0]==1 &&
        state.algorithm.step_sizes[0]==.5f && state.algorithm.position==0 &&
        state.algorithm.leakage_output==0 && state.algorithm.leakage_reference==0);
    for(unsigned i=0;i<1500;i++) CHECK(state.algorithm.history[i]==0);
    params[4].value.str="";params[3].value.str="";
    CHECK(api->prepare(handle,&config)==0);
    for(unsigned i=0;i<24;i++) CHECK(state.algorithm.weights[i]==0);
    CHECK(state.algorithm.step_sizes[0]==0 && state.algorithm.step_sizes[1]==0);
    return api->destroy(handle);
}
