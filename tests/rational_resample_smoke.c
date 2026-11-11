#include "component_state.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do {if(!(x)) {fprintf(stderr,"有理SRC断言失败：%d: %s\n",__LINE__,#x);return 1;}} while(0)
static unsigned allocation,fail_allocation;
void* rational_resample_test_calloc(size_t count,size_t size) {
    return ++allocation==fail_allocation ? NULL : calloc(count,size);
}
static OrpheusValue integer(int value) {
    OrpheusValue result={.type=ORPHEUS_VALUE_INT,.value.i32=value};return result;
}
static unsigned calls,expected_frames;
static uint64_t expected_frame;
static uint32_t expected_flags;
static int delivery_result;
static int deliver(void* user,uint32_t port,const OrpheusDelivery* delivery) {
    (void)user;++calls;
    if(port!=1 || !delivery || !delivery->buffer || delivery->buffer->frame_count!=expected_frames ||
       delivery->frame_index!=expected_frame || delivery->epoch!=9 || delivery->timeline_flags!=expected_flags)
        return ORPHEUS_ERR_INVALID_ARG;
    return delivery_result;
}
static int numerical(uint32_t up,uint32_t down,uint32_t frames,double rate) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    orpheus_builtin_rational_resample_State state;
    const char* ids[]={"up","down","taps","input_frames"};
    OrpheusValue values[]={integer(up),integer(down),integer(128),integer(frames)};
    OrpheusConfig config={.sample_rate=rate,.block_size=frames+1,.channels=2,
        .param_ids=ids,.param_values=values,.param_count=4,.state_block=&state};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && handle==&state && api->prepare(handle,&config)==0);
    float data[2048],output[2048],first[2048];
    expected_frames=frames*up/down;
    CHECK(frames<=1024 && expected_frames<=1024);
    OrpheusBuffer input={.data=data,.format=ORPHEUS_FORMAT_F32,.channels=2,
        .frame_count=frames,.frame_capacity=frames,.interleaved=true};
    OrpheusBuffer out={.data=output,.format=ORPHEUS_FORMAT_F32,.channels=2,
        .frame_capacity=expected_frames,.interleaved=true};
    const OrpheusBuffer* inputs[]={&input};OrpheusBuffer* outputs[]={&out};
    OrpheusContinuation continuation={.emit=deliver};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,
        .frame_count=frames+1,.sample_rate=rate,.epoch=9,.continuation=&continuation};
    const double pi=3.14159265358979323846;
    double error=0,energy=0;delivery_result=0;
    for(unsigned block=0;block<16;++block) {
        for(unsigned f=0;f<frames;++f) {
            data[2*f]=(float)sin(2*pi*1000*(block*frames+f)/rate);data[2*f+1]=0;
        }
        ctx.frame_index=(uint64_t)block*frames;expected_frame=(uint64_t)block*expected_frames;
        ctx.timeline_flags=expected_flags=block ? 0 : ORPHEUS_TIMELINE_DISCONTINUITY;
        unsigned before=calls;
        CHECK(api->process(handle,&ctx)==0 && calls==before+1 && out.frame_count==expected_frames);
        if(!block) memcpy(first,output,expected_frames*2*sizeof(float));
        for(unsigned f=0;f<expected_frames;++f) {
            CHECK(output[2*f+1]==0);
            if(block>2) {
                double position=(double)(block*expected_frames+f)*down/up-(up==down ? 0 : 63.5);
                double target=sin(2*pi*1000*position/rate);
                error+=(output[2*f]-target)*(output[2*f]-target);energy+=target*target;
            }
        }
    }
    CHECK(sqrt(error/energy)<0.005);
    CHECK(api->reset(handle)==0);
    for(unsigned f=0;f<frames;++f) {
        data[2*f]=(float)sin(2*pi*1000*f/rate);data[2*f+1]=0;
    }
    ctx.frame_index=expected_frame=0;
    CHECK(api->process(handle,&ctx)==0 && !memcmp(first,output,expected_frames*2*sizeof(float)));
    if(frames%2==0 && ((frames/2)*up)%down==0) {
        values[3]=integer(frames/2);
        CHECK(api->prepare(handle,&config)==0);
        unsigned full=expected_frames;expected_frames/=2;input.frame_count=frames/2;
        CHECK(api->process(handle,&ctx)==0);
        input.data=data+frames;out.data=output+full;ctx.frame_index=frames/2;expected_frame=full/2;
        CHECK(api->process(handle,&ctx)==0 && !memcmp(first,output,full*2*sizeof(float)));
        input.data=data;out.data=output;input.frame_count=frames;expected_frames=full;
        values[3]=integer(frames);CHECK(api->prepare(handle,&config)==0);
    }
    CHECK(api->reset(handle)==0);
    for(unsigned n=0;n<frames*2;++n) data[n]=1;
    for(unsigned block=0;block<8;++block) {
        ctx.frame_index=(uint64_t)block*frames;expected_frame=(uint64_t)block*expected_frames;
        CHECK(api->process(handle,&ctx)==0);
    }
    for(unsigned n=0;n<expected_frames*2;++n) CHECK(fabs(output[n]-1)<1e-5);
    if(up==3 && down==13) {
        CHECK(api->reset(handle)==0);energy=0;
        for(unsigned block=0;block<16;++block) {
            for(unsigned f=0;f<frames;++f) {
                data[2*f]=(float)sin(2*pi*16000*(block*frames+f)/rate);data[2*f+1]=0;
            }
            ctx.frame_index=(uint64_t)block*frames;expected_frame=(uint64_t)block*expected_frames;
            CHECK(api->process(handle,&ctx)==0);
            if(block>2) for(unsigned f=0;f<expected_frames;++f) energy+=output[2*f]*output[2*f];
        }
        CHECK(sqrt(energy/(13*expected_frames))<0.001);
    }
    out.frame_capacity=expected_frames-1;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->destroy(handle)==0);
    return 0;
}
static int errors_and_lifecycle(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    orpheus_builtin_rational_resample_State state;
    const char* ids[]={"up","down","taps","input_frames"};
    OrpheusValue values[]={integer(3),integer(13),integer(16),integer(13)};
    OrpheusConfig config={.sample_rate=48000,.block_size=7,.channels=2,
        .param_ids=ids,.param_values=values,.param_count=4,.state_block=&state};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0);
    float data[26]={1,2,3,4},output[6];
    OrpheusBuffer input={.data=data,.format=ORPHEUS_FORMAT_F32,.channels=2,
        .frame_count=13,.frame_capacity=13,.interleaved=true};
    OrpheusBuffer out={.data=output,.format=ORPHEUS_FORMAT_F32,.channels=2,
        .frame_capacity=3,.interleaved=true};
    const OrpheusBuffer* inputs[]={&input};OrpheusBuffer* outputs[]={&out};
    OrpheusContinuation continuation={.emit=deliver};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,
        .frame_count=7,.frame_index=130,.epoch=9,.timeline_flags=ORPHEUS_TIMELINE_CONCEALED,.continuation=&continuation};
    expected_frames=3;expected_frame=30;expected_flags=ORPHEUS_TIMELINE_CONCEALED;delivery_result=-77;
    orpheus_builtin_rational_resample_State before=state;
    input.frame_count=12;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&before,&state,sizeof(state)));
    input.frame_count=13;
    const void* aliases[]={&state,state.algorithm.history,state.algorithm.coefficients,output};
    for(unsigned n=0;n<4;++n) {
        input.data=(void*)aliases[n];
        CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&before,&state,sizeof(state)));
    }
    input.data=data;
    for(unsigned n=0;n<3;++n) {
        out.data=(void*)aliases[n];
        CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&before,&state,sizeof(state)));
    }
    out.data=output;ctx.frame_index=131;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&before,&state,sizeof(state)));
    ctx.frame_index=UINT64_MAX-10;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&before,&state,sizeof(state)));
    ctx.frame_index=130;ctx.frame_count=0;unsigned count=calls;
    CHECK(api->process(handle,&ctx)==0 && calls==count && !memcmp(&before,&state,sizeof(state)));
    ctx.frame_count=7;
    CHECK(api->process(handle,&ctx)==-77 && calls==count+1 && state.algorithm.cursor==13);
    CHECK(state.algorithm.history[0]==1 && state.algorithm.history[1]==2);
    before=state;
    for(unsigned n=0;n<4;++n) {
        int32_t saved=values[n].value.i32;values[n]=integer(n==2 ? 15 : 0);
        CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&before,&state,sizeof(state)));
        values[n]=integer(saved);
    }
    values[3]=integer(1);
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&before,&state,sizeof(state)));
    values[3]=integer(13);
    CHECK(api->set_parameter(handle,"up",values)==ORPHEUS_ERR_UNSUPPORTED);
    for(unsigned failed=1;failed<=2;++failed) {
        allocation=0;fail_allocation=failed;
        CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_OUT_OF_MEMORY &&
            !state.algorithm.history && !state.algorithm.coefficients);
        CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_PROCESSING);
        fail_allocation=0;
        CHECK(api->prepare(handle,&config)==0 && state.algorithm.history && state.algorithm.coefficients &&
            state.algorithm.cursor==0 && state.algorithm.history[0]==0);
    }
    ctx.frame_index=(UINT64_MAX/13-1)*13;expected_frame=ctx.frame_index/13*3;delivery_result=0;
    CHECK(api->process(handle,&ctx)==0);
    CHECK(api->destroy(handle)==0);
    return 0;
}
static int identity_bits(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    orpheus_builtin_rational_resample_State state;
    const char* ids[]={"up","down","taps","input_frames"};
    OrpheusValue values[]={integer(2),integer(2),integer(16),integer(2)};
    OrpheusConfig config={.sample_rate=48000,.block_size=2,.channels=2,
        .param_ids=ids,.param_values=values,.param_count=4,.state_block=&state};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0);
    uint32_t data[]={0x80000000u,0x7FC01234u,0x7F800000u,0xFF800000u},output[4];
    OrpheusBuffer input={.data=data,.format=ORPHEUS_FORMAT_F32,.channels=2,
        .frame_count=2,.frame_capacity=2,.interleaved=true};
    OrpheusBuffer out={.data=output,.format=ORPHEUS_FORMAT_F32,.channels=2,
        .frame_capacity=2,.interleaved=true};
    const OrpheusBuffer* inputs[]={&input};OrpheusBuffer* outputs[]={&out};
    OrpheusContinuation continuation={.emit=deliver};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,.frame_count=2,
        .frame_index=UINT64_MAX-2,.epoch=9,.timeline_flags=ORPHEUS_TIMELINE_EOS,.continuation=&continuation};
    expected_frame=ctx.frame_index;expected_frames=2;expected_flags=ORPHEUS_TIMELINE_EOS;delivery_result=0;
    CHECK(api->process(handle,&ctx)==0 && !memcmp(data,output,sizeof(data)) && !state.algorithm.cursor);
    CHECK(api->destroy(handle)==0);
    return 0;
}
int main(void) {
    CHECK(numerical(3,13,416,48000)==0);
    CHECK(numerical(13,3,96,144000.0/13)==0);
    CHECK(numerical(160,147,147,44100)==0);
    CHECK(numerical(1,1,32,48000)==0);
    CHECK(errors_and_lifecycle()==0);
    CHECK(identity_bits()==0);
    return 0;
}
