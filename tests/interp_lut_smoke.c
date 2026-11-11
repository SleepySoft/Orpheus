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
static int shape(const OrpheusComponentInterface* api, unsigned channels, unsigned frames) {
    size_t bytes=(size_t)frames*channels*sizeof(float);
    float* input=malloc(bytes),*output=malloc(bytes);
    CHECK(input && output);
    const uint32_t patterns[]={0x80000000u,0x7fa12345u,0x7fc76543u,1,0x7f800000u,0xff800000u,0x3f800000u};
    for(size_t i=0;i<bytes/4;i++) memcpy(input+i,patterns+i%7,4);
    const char* ids[]={"channels"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=channels}};
    orpheus_builtin_interp_lut_State state;
    OrpheusConfig config={.block_size=frames,.param_count=1,.param_ids=ids,.param_values=params,.state_block=&state};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0);
    CHECK(!strcmp(state.history,"[]"));
    OrpheusBuffer in={.data=input,.format=ORPHEUS_FORMAT_F32,.channels=channels,
        .frame_count=frames,.frame_capacity=frames,.interleaved=true};
    OrpheusBuffer out=in;out.data=output;
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&out};
    Capture capture={0};
    OrpheusContinuation continuation={.emit=emit,.user=&capture};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,
        .frame_count=frames,.continuation=&continuation};
    CHECK(api->process(handle,&ctx)==0 && !memcmp(input,output,bytes) && capture.calls==1);
    capture.fail=true;out.data=input;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_PROCESSING && capture.calls==2);
    CHECK(!strcmp(state.history,"[0,0]"));
    in.frame_count=frames-1;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && capture.calls==2);
    in.frame_count=frames;
    if(frames*channels>1) {
        out.data=input+1;
        CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && capture.calls==2);
    }
    out.data=output;ctx.frame_count=0;capture.fail=false;
    CHECK(api->process(handle,&ctx)==0 && capture.calls==3 && !strcmp(state.history,"[0,0,0]"));
    inputs[0]=NULL;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && capture.calls==3);
    CHECK(api->destroy(handle)==0);
    free(input);free(output);
    return 0;
}
static int history_and_transactions(const OrpheusComponentInterface* api) {
    const char* ids[]={"x_axis","y_axis","x"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_STRING,.value.str="0,10,20"},
        {.type=ORPHEUS_VALUE_STRING,.value.str="0,5,10"},{.type=ORPHEUS_VALUE_FLOAT,.value.f32=5}};
    orpheus_builtin_interp_lut_State state,saved;
    OrpheusConfig config={.block_size=1,.param_count=3,.param_ids=ids,.param_values=params,.state_block=&state};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0);
    const char* invalid[]={"","x","0,,10","0,10,","0,10,10","20,10,0","0,10","0,nan,20"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++) {
        saved=state;params[0].value.str=invalid[i];
        CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&state,&saved,sizeof(state)));
    }
    params[0].value.str="-3.4028234663852886e38,3.4028234663852886e38";
    params[1].value.str="0,1";
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&state,&saved,sizeof(state)));
    params[0].value.str="0,1";
    params[1].value.str="-3.4028234663852886e38,3.4028234663852886e38";
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&state,&saved,sizeof(state)));
    char axis[2048],table[2048];size_t na=0,nt=0;
    for(unsigned i=0;i<257;i++) {
        na+=(size_t)snprintf(axis+na,sizeof(axis)-na,i ? ",%u" : "%u",i);
        nt+=(size_t)snprintf(table+nt,sizeof(table)-nt,i ? ",%.2f" : "%.2f",-(float)i*.25f);
    }
    params[0].value.str=axis;params[1].value.str=table;
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&state,&saved,sizeof(state)));
    params[0].value.str="0,10,20";params[1].value.str="0,5,10";
    float sample=0;
    OrpheusBuffer buffer={.data=&sample,.format=ORPHEUS_FORMAT_F32,.channels=1,
        .frame_count=0,.frame_capacity=1,.interleaved=true};
    const OrpheusBuffer* inputs[]={&buffer};OrpheusBuffer* outputs[]={&buffer};
    Capture capture={0};OrpheusContinuation continuation={.emit=emit,.user=&capture};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,.continuation=&continuation};
    for(unsigned i=0;i<128;i++) {
        OrpheusValue value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=(float)(i%21)*.5f};
        CHECK(api->set_parameter(handle,"x",&value)==0 && api->process(handle,&ctx)==0);
        CHECK(state.y==(float)(i%21)*.25f && capture.calls==i+1);
    }
    char expected[2048];size_t used=1;expected[0]='[';
    for(unsigned i=64;i<128;i++) used+=(size_t)snprintf(expected+used,sizeof(expected)-used,
        i==64 ? "%.4g" : ",%.4g",(float)(i%21)*.25f);
    expected[used++]=']';expected[used]=0;
    CHECK(state.algorithm.hist_count==64 && !strcmp(state.history,expected));
    OrpheusValue value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=NAN};
    saved=state;
    CHECK(api->set_parameter(handle,"x",&value)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&state,&saved,sizeof(state)));
    value.value.f32=1;
    CHECK(api->set_parameter(handle,"y",&value)!=0 && api->set_parameter(handle,"history",&value)!=0);
    CHECK(!memcmp(&state,&saved,sizeof(state)));
    CHECK(api->reset(handle)==0 && state.x==0 && state.y==0 && !strcmp(state.history,"[]"));
    CHECK(state.algorithm.count==3 && state.algorithm.table[2]==10);
    CHECK(api->destroy(handle)==0);
    return 0;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    CHECK(history_and_transactions(api)==0);
    CHECK(shape(api,1,1)==0);
    CHECK(shape(api,3,17)==0);
    CHECK(shape(api,32,65536)==0);
    return 0;
}
