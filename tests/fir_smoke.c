#include "component_state.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x);return 1; } } while(0)
typedef struct { unsigned calls; bool fail; } Capture;
static int emit(void* user, uint32_t port, const OrpheusDelivery* delivery) {
    Capture* capture=user;
    if(port!=1 || !delivery || !delivery->buffer) return ORPHEUS_ERR_INVALID_ARG;
    capture->calls++;
    return capture->fail ? ORPHEUS_ERR_PROCESSING : ORPHEUS_OK;
}
static float sample(unsigned n, unsigned c) {
    return (float)((int)((n*17+c*31)%103)-51)*.03125f;
}
static int shape(const OrpheusComponentInterface* api, unsigned channels, unsigned frames) {
    size_t bytes=(size_t)frames*channels*sizeof(float);
    float* input=malloc(bytes),*output=malloc(bytes);
    CHECK(input && output);
    const char* ids[]={"channels","coefficients"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=channels},
        {.type=ORPHEUS_VALUE_STRING,.value.str=".25,.5,.25"}};
    orpheus_builtin_fir_State state;
    OrpheusConfig config={.block_size=frames,.param_count=2,.param_ids=ids,.param_values=params,.state_block=&state};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0 && state.taps==3);
    OrpheusBuffer in={.data=input,.format=ORPHEUS_FORMAT_F32,.channels=channels,
        .frame_count=frames,.frame_capacity=frames,.interleaved=true};
    OrpheusBuffer out=in;out.data=output;
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&out};
    Capture capture={0};
    OrpheusContinuation continuation={.emit=emit,.user=&capture};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,
        .frame_count=frames,.continuation=&continuation};
    for(unsigned pass=0;pass<3;pass++) {
        for(unsigned n=0;n<frames;n++) for(unsigned c=0;c<channels;c++)
            input[(size_t)n*channels+c]=sample(pass*frames+n,c);
        out.data=pass==1 ? input : output;capture.fail=pass==1;
        CHECK(api->process(handle,&ctx)==(pass==1 ? ORPHEUS_ERR_PROCESSING : ORPHEUS_OK));
        CHECK(capture.calls==pass+1);
        for(unsigned n=0;n<frames;n++) for(unsigned c=0;c<channels;c++) {
            unsigned frame=pass*frames+n;
            float expected=.25f*sample(frame,c);
            if(frame>=1) expected+=.5f*sample(frame-1,c);
            if(frame>=2) expected+=.25f*sample(frame-2,c);
            CHECK(!memcmp((float*)out.data+(size_t)n*channels+c,&expected,sizeof(float)));
        }
    }
    uint32_t pos=state.algorithm.pos[0];
    in.frame_count=frames-1;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && state.algorithm.pos[0]==pos);
    in.frame_count=frames;
    if(frames*channels>1) {
        out.data=input+1;
        CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && state.algorithm.pos[0]==pos);
    }
    out.data=state.algorithm.delay;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && state.algorithm.pos[0]==pos);
    inputs[0]=NULL;outputs[0]=NULL;ctx.frame_count=0;
    CHECK(api->process(handle,&ctx)==0 && capture.calls==3);
    CHECK(api->reset(handle)==0 && state.taps==3 && state.algorithm.coeffs[1]==.5f);
    for(unsigned c=0;c<channels;c++) CHECK(state.algorithm.pos[c]==0);
    for(unsigned i=0;i<3*channels;i++) CHECK(state.algorithm.delay[i]==0);
    CHECK(api->destroy(handle)==0 && !state.algorithm.coeffs && !state.algorithm.delay && !state.algorithm.pos);
    free(input);free(output);
    return 0;
}
static int transactions(const OrpheusComponentInterface* api) {
    const char* ids[]={"channels","coefficients"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=1},
        {.type=ORPHEUS_VALUE_STRING,.value.str="1,0"}};
    orpheus_builtin_fir_State state,saved;
    OrpheusConfig config={.block_size=3,.param_count=2,.param_ids=ids,.param_values=params,.state_block=&state};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0);
    float input[]={NAN,1,1},output[3];
    OrpheusBuffer in={.data=input,.format=ORPHEUS_FORMAT_F32,.channels=1,
        .frame_count=3,.frame_capacity=3,.interleaved=true};
    OrpheusBuffer out=in;out.data=output;
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&out};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,.frame_count=3};
    CHECK(api->process(handle,&ctx)==0 && isnan(output[0]) && isnan(output[1]) && output[2]==1);
    const char* invalid[]={"x","1x","1,,2","1,","1, ","nan","inf","-inf","1e9999","1e39"};
    float history[2];memcpy(history,state.algorithm.delay,sizeof(history));
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++) {
        saved=state;params[1].value.str=invalid[i];
        CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&state,&saved,sizeof(state)));
        CHECK(!memcmp(history,state.algorithm.delay,sizeof(history)));
    }
    char boundary[2051];
    for(unsigned i=0;i<1025;i++) { boundary[i*2]='1';boundary[i*2+1]=','; }
    boundary[2049]=0;params[1].value.str=boundary;
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&state,&saved,sizeof(state)));
    boundary[2047]=0;
    CHECK(api->prepare(handle,&config)==0 && state.taps==1024 && state.algorithm.coeffs[1023]==1);
    OrpheusValue value={.type=ORPHEUS_VALUE_INT,.value.i32=1};
    CHECK(api->set_parameter(handle,"taps",&value)!=0);
    CHECK(api->get_parameter(handle,"taps",&value)==0 && value.type==ORPHEUS_VALUE_INT && value.value.i32==1024);
    params[1].value.str=" \t\n";
    CHECK(api->prepare(handle,&config)==0 && state.taps==1 && state.algorithm.coeffs[0]==1);
    CHECK(api->destroy(handle)==0);
    return 0;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    CHECK(transactions(api)==0);
    CHECK(shape(api,1,1)==0);
    CHECK(shape(api,3,7)==0);
    CHECK(shape(api,32,65536)==0);
    return 0;
}
