#include "component_state.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if(!(expr)) { fprintf(stderr,"%d: %s\n",__LINE__,#expr);return 1; } } while(0)
typedef orpheus_builtin_sweep_record_State State;
typedef struct { State* state;unsigned calls;int error;OrpheusSlotInfo sweep; } Capture;
static OrpheusSlotId add(void* user,const OrpheusSlotInfo* slot) {
    Capture* capture=user;
    if(!strcmp(slot->key,"sweep")) capture->sweep=*slot;
    return 0;
}
static int emit(void* user,uint32_t port,const OrpheusDelivery* delivery) {
    Capture* capture=user;
    if(port!=1 || !delivery || !delivery->buffer || delivery->frame_index!=333 ||
        capture->state->progress<=0) return ORPHEUS_ERR_INVALID_ARG;
    capture->calls++;
    return capture->error;
}

int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    State state;
    const char* ids[]={"start_freq","end_freq","duration_s","log_scale","bins"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_FLOAT,.value.f32=1},
        {.type=ORPHEUS_VALUE_FLOAT,.value.f32=9},{.type=ORPHEUS_VALUE_FLOAT,.value.f32=1},
        {.type=ORPHEUS_VALUE_BOOL,.value.b=false},{.type=ORPHEUS_VALUE_INT,.value.i32=8}};
    OrpheusConfig config={.channels=2,.sample_rate=16,.param_count=5,.param_ids=ids,
        .param_values=params,.state_block=&state};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0);
    Capture capture={.state=&state};
    OrpheusRegistry registry={.ctx=&capture,.add=add};
    CHECK(api->register_slots(handle,&registry)==0 && capture.sweep.count==512 &&
        capture.sweep.kind==ORPHEUS_SLOT_PROBE && capture.sweep.offset==offsetof(State,sweep));
    float input[8],output[8],acc[8]={0};
    uint64_t count[8]={0},total=0;
    OrpheusBuffer in={.data=input,.channels=2,.format=ORPHEUS_FORMAT_F32,.interleaved=true,
        .frame_count=3,.frame_capacity=4},out=in;
    out.data=output;
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&out};
    OrpheusContinuation continuation={.user=&capture,.emit=emit};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,
        .frame_count=3,.sample_rate=16,.frame_index=333,.continuation=&continuation};
    const unsigned lengths[]={3,1,2};
    for(unsigned call=0;call<9;call++) {
        unsigned frames=lengths[call%3];
        ctx.frame_count=frames;in.frame_count=frames;capture.error=call==0 ? -77 : 0;
        out.data=call==3 ? input : output;
        for(unsigned n=0;n<frames;n++) {
            input[2*n]=.25f+(float)((total+n)%5)*.125f;input[2*n+1]=1000;
        }
        bool done=total>=16;
        unsigned bin=(unsigned)((double)total/16*7+.5);
        if(!done) {
            double sum=0;
            for(unsigned n=0;n<frames;n++) { double x=input[2*n];sum+=x*x; }
            acc[bin]+=(float)sum;count[bin]+=frames;
        }
        CHECK(api->process(handle,&ctx)==capture.error && capture.calls==call+1 &&
            !memcmp(out.data,input,frames*2*sizeof(float)));
        if(!done) {
            float frequency=(float)(1+(double)total/16*8);
            total+=frames;
            CHECK(state.current_freq==frequency && state.algorithm.total_frames==total);
        }
        CHECK(state.algorithm.done==(total>=16));
        unsigned measured=0;
        for(unsigned i=0;i<8;i++) {
            float frequency=1+8*((float)i/7);
            float magnitude=count[i] ? sqrtf(acc[i]/(float)count[i]) : 0;
            CHECK(!memcmp(state.sweep+i,&frequency,sizeof(float)) &&
                !memcmp(state.sweep+256+i,&magnitude,sizeof(float)) &&
                state.algorithm.count[i]==count[i] && state.algorithm.acc[i]==acc[i]);
            measured+=count[i]>0;
        }
        CHECK(state.progress==(total>=16 ? 1 : (float)measured/8));
    }
    out.data=output;
    State saved=state;
    params[4].value.i32=257;
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&saved,&state,sizeof(state)));
    params[4].value.i32=8;
    inputs[0]=NULL;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);inputs[0]=&in;
    outputs[0]=NULL;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);outputs[0]=&out;
    in.frame_count=1;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);in.frame_count=2;
    out.frame_count=1;out.frame_capacity=1;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);out.frame_count=2;out.frame_capacity=4;
    ctx.sample_rate=0;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);ctx.sample_rate=16;
    out.data=input+1;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    out.data=state.sweep;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);out.data=output;
    in.data=state.algorithm.acc;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);in.data=input;
    CHECK(capture.calls==9 && !memcmp(&saved,&state,sizeof(state)));
    ctx.frame_count=0;inputs[0]=NULL;outputs[0]=NULL;
    CHECK(api->process(handle,&ctx)==0 && !memcmp(&saved,&state,sizeof(state)));
    inputs[0]=&in;outputs[0]=&out;ctx.frame_count=2;
    CHECK(api->reset(handle)==0);
    state.algorithm.total_frames=UINT64_MAX;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_PROCESSING);state.algorithm.total_frames=0;
    state.algorithm.count[0]=UINT64_MAX;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_PROCESSING);state.algorithm.count[0]=0;
    state.algorithm.quiet_frames=UINT64_MAX;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_PROCESSING);state.algorithm.quiet_frames=0;
    CHECK(state.progress==0 && state.current_freq==0 && !state.algorithm.done);
    for(unsigned i=0;i<256;i++) CHECK(state.sweep[256+i]==0);
    params[2].value.f32=0;params[0].value.f32=4;params[1].value.f32=4;
    config.sample_rate=2.5;
    CHECK(api->prepare(handle,&config)==0);
    ctx.sample_rate=2.5;ctx.frame_count=1;in.frame_count=1;capture.error=0;
    input[0]=1;input[1]=1000;
    CHECK(api->process(handle,&ctx)==0 && !state.algorithm.done && state.current_freq==4);
    input[0]=0;
    CHECK(api->process(handle,&ctx)==0 && state.algorithm.done && state.progress==1 &&
        state.algorithm.quiet_frames==1 && state.algorithm.count[0]==2);
    for(unsigned i=0;i<8;i++) CHECK(state.sweep[i]==4);
    return api->destroy(handle);
}
