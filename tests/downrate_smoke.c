#include "component_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do {if(!(x)) {fprintf(stderr,"%d: %s\n",__LINE__,#x);return 1;}} while(0)
static bool fail_allocate;
void* downrate_test_calloc(size_t count,size_t size) {
    return fail_allocate ? NULL : calloc(count,size);
}
static unsigned calls;
static float expected[12];
static int deliver(void* user,uint32_t port,const OrpheusDelivery* delivery) {
    (void)user;++calls;
    if(port!=1 || !delivery || !delivery->buffer || delivery->buffer->frame_count!=6 ||
       delivery->frame_index!=100 || delivery->epoch!=9 ||
       delivery->timeline_flags!=(ORPHEUS_TIMELINE_DISCONTINUITY|ORPHEUS_TIMELINE_CONCEALED|ORPHEUS_TIMELINE_EOS) ||
       memcmp(delivery->buffer->data,expected,sizeof(expected))) return ORPHEUS_ERR_INVALID_ARG;
    return -77;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    orpheus_builtin_downrate_State state;
    const char* ids[]={"factor","input_frames","channels"};
    OrpheusValue values[]={
        {.type=ORPHEUS_VALUE_INT,.value.i32=3},
        {.type=ORPHEUS_VALUE_INT,.value.i32=2},
        {.type=ORPHEUS_VALUE_INT,.value.i32=2},
    };
    OrpheusConfig config={.block_size=2,.channels=2,.param_ids=ids,.param_values=values,
        .param_count=3,.state_block=&state};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && handle==&state && api->prepare(handle,&config)==0);
    for(unsigned n=0;n<12;++n) expected[n]=(float)((int)n-6)*.125f;
    expected[1]=-0.0f;
    float data[4],output[12];
    OrpheusBuffer input={.data=data,.channels=2,.frame_count=2,.frame_capacity=2,
        .format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer out={.data=output,.channels=2,.frame_capacity=6,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    const OrpheusBuffer* inputs[]={&input,NULL};OrpheusBuffer* outputs[]={&out};
    OrpheusContinuation continuation={.emit=deliver};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=2,.output_count=1,
        .frame_count=2,.epoch=9,.continuation=&continuation,.trigger_input=1};
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_PROCESSING && state.underflows==1 && !calls);
    for(unsigned part=0;part<3;++part) {
        memcpy(data,expected+part*4,sizeof(data));
        ctx.trigger_input=0;ctx.frame_index=100+part*2;
        ctx.timeline_flags=part==0 ? ORPHEUS_TIMELINE_DISCONTINUITY :
            part==1 ? ORPHEUS_TIMELINE_CONCEALED : ORPHEUS_TIMELINE_EOS;
        if(part==1) {
            ctx.epoch=10;
            CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && state.available_frames==2);
            ctx.epoch=9;ctx.frame_index=999;
            CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && state.available_frames==2);
            ctx.frame_index=102;ctx.timeline_flags=ORPHEUS_TIMELINE_DISCONTINUITY;
            CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && state.available_frames==2);
            ctx.timeline_flags=ORPHEUS_TIMELINE_CONCEALED;
        }
        CHECK(api->process(handle,&ctx)==0 && state.available_frames==(int32_t)(part+1)*2 && !calls);
        for(unsigned n=0;n<4;++n) data[n]=999.0f;
        if(part==0) {
            ctx.trigger_input=1;
            CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_PROCESSING && state.available_frames==2 && !calls);
        }
    }
    ctx.trigger_input=0;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_PROCESSING && state.overflows==1 && state.available_frames==6);
    state.overflows=INT32_MAX;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_PROCESSING && state.overflows==INT32_MAX);
    ctx.frame_count=0;
    CHECK(api->process(handle,&ctx)==0 && state.available_frames==6 && !calls);
    ctx.frame_count=2;ctx.trigger_input=1;ctx.frame_index=777;ctx.epoch=31;ctx.timeline_flags=ORPHEUS_TIMELINE_DROPPED;
    out.data=state.algorithm.block;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && state.available_frames==6);
    out.data=output;
    CHECK(api->process(handle,&ctx)==-77 && !state.available_frames && calls==1);
    CHECK(!memcmp(output,expected,sizeof(output)));
    state.underflows=INT32_MAX;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_PROCESSING && state.underflows==INT32_MAX && calls==1);
    CHECK(api->reset(handle)==0 && !state.underflows && !state.overflows && !state.available_frames);
    for(unsigned n=0;n<12;++n) CHECK(state.algorithm.block[n]==0);
    ctx.trigger_input=0;ctx.epoch=9;ctx.frame_index=100;ctx.timeline_flags=0;
    input.data=&state.channels;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !state.available_frames);
    input.data=data;ctx.frame_index=UINT64_MAX-3;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !state.available_frames);
    ctx.frame_index=100;ctx.timeline_flags=ORPHEUS_TIMELINE_EOS;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !state.available_frames);
    CHECK(api->set_parameter(handle,"input_frames",values+1)==ORPHEUS_ERR_UNSUPPORTED);
    CHECK(api->set_parameter(handle,"available_frames",values+1)==ORPHEUS_ERR_UNSUPPORTED);
    orpheus_builtin_downrate_State before=state;
    values[0].value.i32=0;
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&before,&state,sizeof(state)));
    values[0].value.i32=3;fail_allocate=true;
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_OUT_OF_MEMORY && !state.algorithm.block);
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_PROCESSING);
    fail_allocate=false;
    CHECK(api->prepare(handle,&config)==0 && state.algorithm.block && !state.available_frames);
    CHECK(api->destroy(handle)==0);
    return 0;
}
