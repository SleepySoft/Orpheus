#include "component_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do {if(!(x)) {fprintf(stderr,"降采样断言失败：%d: %s\n",__LINE__,#x);return 1;}} while(0)
static bool fail_allocate;
void* resample_test_calloc(size_t count,size_t size) {
    return fail_allocate ? NULL : calloc(count,size);
}
static unsigned calls,output_frames,channels;
static float expected[2048];
static int delivery_result;
static uint32_t flags;
static int deliver(void* user,uint32_t port,const OrpheusDelivery* delivery) {
    (void)user;++calls;
    if(port!=1 || !delivery || !delivery->buffer || delivery->buffer->frame_count!=output_frames ||
       delivery->frame_index!=100 || delivery->epoch!=9 || delivery->timeline_flags!=flags ||
       memcmp(delivery->buffer->data,expected,(size_t)output_frames*channels*sizeof(float)))
        return ORPHEUS_ERR_INVALID_ARG;
    return delivery_result;
}
static OrpheusValue integer(int value) {
    OrpheusValue result={.type=ORPHEUS_VALUE_INT,.value.i32=value};return result;
}
static int numerical(unsigned factor,unsigned mode,unsigned phase,unsigned ch,unsigned frames) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    orpheus_builtin_resample_State state;
    const char* ids[]={"factor","mode","phase","channels","input_frames","output_frames"};
    channels=ch;output_frames=frames*2;
    OrpheusValue values[]={integer(factor),integer(mode),integer(phase),integer(ch),
        integer(frames),integer(output_frames)};
    OrpheusConfig config={.sample_rate=48000,.block_size=frames+1,.channels=ch,
        .param_ids=ids,.param_values=values,.param_count=6,.state_block=&state};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && handle==&state && api->prepare(handle,&config)==0);
    float data[224],output[448];
    OrpheusBuffer input={.data=data,.format=ORPHEUS_FORMAT_F32,.channels=ch,
        .frame_count=frames,.frame_capacity=frames,.interleaved=true};
    OrpheusBuffer out={.data=output,.format=ORPHEUS_FORMAT_F32,.channels=ch,
        .frame_capacity=output_frames,.interleaved=true};
    const OrpheusBuffer* inputs[]={&input,NULL};OrpheusBuffer* outputs[]={&out};
    OrpheusContinuation continuation={.emit=deliver};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=2,.output_count=1,
        .frame_count=frames+1,.epoch=9,.continuation=&continuation};
    flags=ORPHEUS_TIMELINE_DISCONTINUITY|ORPHEUS_TIMELINE_CONCEALED|ORPHEUS_TIMELINE_EOS;
    for(unsigned f=0;f<output_frames;++f) for(unsigned c=0;c<ch;++c) {
        float sample=mode ? (float)(f*factor+phase+1) : f*factor+((float)factor+1)*.5f;
        expected[f*ch+c]=c%2 ? -sample : sample;
    }
    for(unsigned pass=0;pass<2;++pass) {
        CHECK(api->reset(handle)==0);
        unsigned before=calls;
        for(unsigned n=0;n<output_frames*ch;++n) output[n]=9999;
        for(unsigned part=0;part<2*factor;++part) {
            for(unsigned f=0;f<frames;++f) for(unsigned c=0;c<ch;++c)
                data[f*ch+c]=(c%2 ? -1.0f : 1.0f)*(part*frames+f+1);
            ctx.trigger_input=0;ctx.frame_index=100*factor+part*frames;
            ctx.timeline_flags=part==0 ? ORPHEUS_TIMELINE_DISCONTINUITY :
                part==2*factor-1 ? ORPHEUS_TIMELINE_EOS : ORPHEUS_TIMELINE_CONCEALED;
            CHECK(api->process(handle,&ctx)==0 && calls==before);
            CHECK(state.available_frames==(int32_t)((part+1)*frames/factor));
            CHECK(state.algorithm.pending==((part+1)*frames)%factor);
            memset(data,0,sizeof(data));
            for(unsigned n=0;n<output_frames*ch;++n) CHECK(output[n]==9999);
            if(part==0) {
                ctx.trigger_input=1;
                CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_PROCESSING && calls==before);
            }
        }
        ctx.trigger_input=0;
        CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_PROCESSING && state.overflows==1);
        state.overflows=INT32_MAX;
        CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_PROCESSING && state.overflows==INT32_MAX);
        ctx.frame_count=0;ctx.trigger_input=1;
        CHECK(api->process(handle,&ctx)==0 && calls==before && state.available_frames==(int32_t)output_frames);
        ctx.frame_count=frames+1;ctx.frame_index=9999;ctx.epoch=17;ctx.timeline_flags=ORPHEUS_TIMELINE_DROPPED;
        out.data=state.algorithm.block;
        CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && state.available_frames==(int32_t)output_frames);
        out.data=&state.channels;
        CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && state.available_frames==(int32_t)output_frames);
        out.data=output;delivery_result=pass ? -77 : 0;
        CHECK(api->process(handle,&ctx)==delivery_result && calls==before+1 && !state.available_frames);
        CHECK(!memcmp(output,expected,(size_t)output_frames*ch*sizeof(float)));
        state.underflows=INT32_MAX;
        CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_PROCESSING && state.underflows==INT32_MAX && calls==before+1);
        ctx.epoch=9;
    }
    OrpheusValue value;
    CHECK(api->get_parameter(handle,"mode",&value)==0 && value.value.i32==(int32_t)mode);
    CHECK(api->get_parameter(handle,"phase",&value)==0 && value.value.i32==(int32_t)phase);
    CHECK(api->set_parameter(handle,"mode",values+1)==ORPHEUS_ERR_UNSUPPORTED);
    CHECK(api->set_parameter(handle,"available_frames",values)==ORPHEUS_ERR_UNSUPPORTED);
    CHECK(api->destroy(handle)==0);
    return 0;
}
static int errors_and_lifecycle(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    orpheus_builtin_resample_State state;
    const char* ids[]={"factor","input_frames","output_frames","channels","mode","phase","available_frames"};
    OrpheusValue values[]={integer(3),integer(2),integer(2),integer(2),integer(0),integer(0),integer(1)};
    OrpheusConfig config={.sample_rate=48000,.block_size=2,.channels=2,
        .param_ids=ids,.param_values=values,.param_count=6,.state_block=&state};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0);
    float data[4]={1,2,3,4},output[4];
    OrpheusBuffer input={.data=data,.format=ORPHEUS_FORMAT_F32,.channels=2,
        .frame_count=2,.frame_capacity=2,.interleaved=true};
    OrpheusBuffer out={.data=output,.format=ORPHEUS_FORMAT_F32,.channels=2,
        .frame_capacity=2,.interleaved=true};
    const OrpheusBuffer* inputs[]={&input,NULL};OrpheusBuffer* outputs[]={&out};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=2,.output_count=1,
        .frame_count=2,.epoch=9,.frame_index=300};
    input.frame_count=1;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    input.frame_count=2;input.data=&state.channels;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    input.data=state.algorithm.block;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    input.data=data;ctx.frame_index=301;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    ctx.frame_index=UINT64_MAX-3;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    ctx.frame_index=300;ctx.timeline_flags=ORPHEUS_TIMELINE_EOS;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    ctx.timeline_flags=0;
    CHECK(api->process(handle,&ctx)==0 && state.algorithm.pending==2 && !state.available_frames);
    orpheus_builtin_resample_State before=state;
    ctx.frame_index=302;ctx.epoch=10;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&before,&state,sizeof(state)));
    ctx.epoch=9;ctx.frame_index=303;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&before,&state,sizeof(state)));
    ctx.frame_index=302;ctx.timeline_flags=ORPHEUS_TIMELINE_DISCONTINUITY;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&before,&state,sizeof(state)));
    ctx.timeline_flags=ORPHEUS_TIMELINE_EOS;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&before,&state,sizeof(state)));
    values[4].value.i32=2;
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&before,&state,sizeof(state)));
    values[4].value.i32=0;values[5].value.i32=3;
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&before,&state,sizeof(state)));
    values[5].value.i32=0;config.param_count=7;
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&before,&state,sizeof(state)));
    config.param_count=6;
    for(unsigned which=0;which<3;++which) {
        values[which].value.i32=which ? 0 : 1;
        CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&before,&state,sizeof(state)));
        values[which].value.i32=which ? 2 : 3;
    }
    values[1].value.i32=4;
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&before,&state,sizeof(state)));
    values[1].value.i32=2;fail_allocate=true;
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_OUT_OF_MEMORY && !state.algorithm.block);
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_PROCESSING);
    fail_allocate=false;
    CHECK(api->prepare(handle,&config)==0 && state.algorithm.block && !state.algorithm.pending && !state.available_frames);
    CHECK(api->destroy(handle)==0);
    return 0;
}
static int selection_bits(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    orpheus_builtin_resample_State state;
    const char* ids[]={"factor","mode","input_frames","output_frames"};
    OrpheusValue values[]={integer(2),integer(1),integer(2),integer(1)};
    OrpheusConfig config={.sample_rate=48000,.block_size=2,.channels=2,
        .param_ids=ids,.param_values=values,.param_count=4,.state_block=&state};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0);
    uint32_t data[]={0x80000000u,0x7FC01234u,0x7FC07890u,0x7F800000u},output[2];
    OrpheusBuffer input={.data=data,.format=ORPHEUS_FORMAT_F32,.channels=2,
        .frame_count=2,.frame_capacity=2,.interleaved=true};
    OrpheusBuffer out={.data=output,.format=ORPHEUS_FORMAT_F32,.channels=2,.frame_capacity=1,.interleaved=true};
    const OrpheusBuffer* inputs[]={&input,NULL};OrpheusBuffer* outputs[]={&out};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=2,.output_count=1,.frame_count=2};
    CHECK(api->process(handle,&ctx)==0);
    ctx.trigger_input=1;
    CHECK(api->process(handle,&ctx)==0 && !memcmp(data,output,sizeof(output)));
    out.frame_capacity=0;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->destroy(handle)==0);
    return 0;
}
int main(void) {
    const unsigned factors[]={2,3,16,64};
    for(unsigned n=0;n<4;++n) for(unsigned mode=0;mode<2;++mode) for(unsigned last=0;last<2;++last)
        CHECK(numerical(factors[n],mode,last ? factors[n]-1 : 0,n==3 ? 32 : 2,n%2 ? 7 : 1)==0);
    CHECK(errors_and_lifecycle()==0);
    CHECK(selection_bits()==0);
    return 0;
}
