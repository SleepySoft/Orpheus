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
static int shape(const OrpheusComponentInterface* api, unsigned cin, unsigned cout, unsigned frames) {
    size_t input_bytes=(size_t)frames*cin*sizeof(float),output_bytes=(size_t)frames*cout*sizeof(float);
    float* input=malloc(input_bytes),*output=malloc(output_bytes);
    CHECK(input && output);
    for(size_t i=0;i<(size_t)frames*cin;i++) input[i]=(float)((int)(i%103)-51)*.03125f;
    const char* ids[]={"channels_in","channels_out","matrix"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=cin},
        {.type=ORPHEUS_VALUE_INT,.value.i32=cout},{.type=ORPHEUS_VALUE_STRING,.value.str=".25"}};
    orpheus_builtin_output_router_State state;
    OrpheusConfig config={.sample_rate=48000,.block_size=frames,.param_count=3,.param_ids=ids,
        .param_values=params,.state_block=&state};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0);
    OrpheusBuffer in={.data=input,.format=ORPHEUS_FORMAT_F32,.channels=cin,
        .frame_count=frames,.frame_capacity=frames,.interleaved=true};
    OrpheusBuffer out=in;out.channels=cout;out.data=output;
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&out};
    Capture capture={0};
    OrpheusContinuation continuation={.emit=emit,.user=&capture};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,
        .frame_count=frames,.continuation=&continuation};
    for(unsigned pass=0;pass<2;pass++) {
        capture.fail=pass!=0;
        CHECK(api->process(handle,&ctx)==(pass ? ORPHEUS_ERR_PROCESSING : ORPHEUS_OK));
        CHECK(capture.calls==pass+1);
        for(unsigned n=0;n<frames;n++) for(unsigned c=0;c<cout;c++) {
            float expected=c<cin ? input[(size_t)n*cin+c]*(c==0 ? .25f : 1.0f) : 0.0f;
            CHECK(output[(size_t)n*cout+c]==expected);
        }
    }
    in.frame_count=frames-1;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && capture.calls==2);
    in.frame_count=frames;out.data=input;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && capture.calls==2);
    out.data=state.algorithm.matrix;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && capture.calls==2);
    inputs[0]=NULL;outputs[0]=NULL;ctx.frame_count=0;
    CHECK(api->process(handle,&ctx)==0 && capture.calls==2);
    CHECK(api->destroy(handle)==0);
    free(input);free(output);
    return 0;
}

static int transactions(const OrpheusComponentInterface* api) {
    const char* ids[]={"channels_in","channels_out","matrix"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=4},
        {.type=ORPHEUS_VALUE_INT,.value.i32=1},
        {.type=ORPHEUS_VALUE_STRING,.value.str="1,-0,0,0"}};
    orpheus_builtin_output_router_State state,saved;
    OrpheusConfig config={.block_size=1,.param_count=3,.param_ids=ids,.param_values=params,.state_block=&state};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0);
    float input[]={2,0,INFINITY,-INFINITY},output;
    uint32_t nan=0x7fa12345u;memcpy(input+1,&nan,sizeof(nan));
    OrpheusBuffer in={.data=input,.format=ORPHEUS_FORMAT_F32,.channels=4,
        .frame_count=1,.frame_capacity=1,.interleaved=true};
    OrpheusBuffer out=in;out.data=&output;out.channels=1;
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&out};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,.frame_count=1};
    CHECK(api->process(handle,&ctx)==0 && output==2.0f);
    const char* invalid[]={"x","identityx","1x","1,,2","1,","1, ","nan","inf","1e9999","1,2,3,4,5"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++) {
        saved=state;params[2].value.str=invalid[i];
        CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG);
        CHECK(!memcmp(&state,&saved,sizeof(state)));
    }
    params[2].value.str="0,0,-0,0";
    CHECK(api->prepare(handle,&config)==0 && api->process(handle,&ctx)==0);
    uint32_t zero;memcpy(&zero,&output,sizeof(zero));CHECK(zero==0);
    saved=state;
    CHECK(api->reset(handle)==0 && !memcmp(&state,&saved,sizeof(state)));
    OrpheusValue readback;
    CHECK(api->get_parameter(handle,"matrix",&readback)==0 && !strcmp(readback.value.str,"0,0,-0,0"));
    params[2].value.str=" \tidentity \n";
    CHECK(api->prepare(handle,&config)==0 && api->process(handle,&ctx)==0 && output==2.0f);
    CHECK(api->destroy(handle)==0);
    return 0;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    CHECK(transactions(api)==0);
    CHECK(shape(api,1,7,1)==0);
    CHECK(shape(api,7,1,17)==0);
    CHECK(shape(api,32,32,65536)==0);
    return 0;
}
