#include "component_state.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if(!(expr)) { fprintf(stderr,"%d: %s\n",__LINE__,#expr);return 1; } } while(0)

static int downstream(void* user,uint32_t port,const OrpheusDelivery* delivery) {
    unsigned* calls=user;
    if(port!=1 || !delivery || !delivery->buffer) return ORPHEUS_ERR_INVALID_ARG;
    return ++*calls==3 ? ORPHEUS_ERR_PROCESSING : ORPHEUS_OK;
}

int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    orpheus_builtin_indexed_gain_State state={0};
    const char* ids[]={"channels","apply_index_on_prepare","initial_gain","table_idx","table_db"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=64},
        {.type=ORPHEUS_VALUE_BOOL,.value.b=false},{.type=ORPHEUS_VALUE_FLOAT,.value.f32=.5f},
        {.type=ORPHEUS_VALUE_STRING,.value.str="0,128,255,255"},
        {.type=ORPHEUS_VALUE_STRING,.value.str="-24,-12,0,0"}};
    OrpheusConfig config={.sample_rate=48000,.block_size=65536,.param_count=5,
        .param_ids=ids,.param_values=params,.state_block=&state};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && handle==&state && api->prepare(handle,&config)==0);
    CHECK(state.current_gain==.5f && state.algorithm.table_size==4);
    const size_t count=64u*65536u,bytes=count*sizeof(float);
    float* input=malloc(bytes),*output=malloc(bytes);CHECK(input && output);
    for(size_t n=0;n<count;n++) input[n]=(float)((int)(n%97)-48)*.03125f;
    OrpheusBuffer in={.data=input,.channels=64,.format=ORPHEUS_FORMAT_F32,.interleaved=true,
        .frame_count=65536,.frame_capacity=65536};
    OrpheusBuffer out=in;out.data=output;
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&out};
    unsigned calls=0;OrpheusContinuation continuation={.emit=downstream,.user=&calls};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,
        .frame_count=65536,.continuation=&continuation};
    for(unsigned pass=0;pass<4;pass++) {
        CHECK(api->process(handle,&ctx)==(pass==2 ? ORPHEUS_ERR_PROCESSING : ORPHEUS_OK));
        CHECK(calls==pass+1);
        for(size_t n=0;n<count;n++) CHECK(output[n]==input[n]*.5f);
    }
    OrpheusValue value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=128};
    CHECK(api->set_parameter(handle,"gain_index",&value)==0 && state.algorithm.remaining==1);
    float before=state.current_gain,coefficient=state.algorithm.ramp_coeff;
    in.frame_count=65535;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && state.current_gain==before &&
          state.algorithm.remaining==1 && state.algorithm.ramp_coeff==coefficient && calls==4);
    in.frame_count=65536;out.data=input+1;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && state.current_gain==before);
    out.data=&state;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && state.current_gain==before);
    out.data=output;
    value.type=ORPHEUS_VALUE_BOOL;value.value.b=true;
    CHECK(api->set_parameter(handle,"freeze",&value)==0);
    CHECK(api->process(handle,&ctx)==0 && state.current_gain==before && state.algorithm.remaining==1);
    orpheus_builtin_indexed_gain_State saved=state;
    const char* invalid[]={"","1,,2","1,2,","nan,2","1x,2","1,2,3","2,1,255,255","0,1e99,255,255"};
    for(unsigned n=0;n<sizeof(invalid)/sizeof(*invalid);n++) {
        params[3].value.str=invalid[n];
        CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&saved,&state,sizeof(state)));
    }
    params[3].value.str="0,128,255,255";
    char excessive[128]="";
    for(unsigned n=0;n<33;n++) strcat(excessive,n ? ",0" : "0");
    params[3].value.str=params[4].value.str=excessive;
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&saved,&state,sizeof(state)));
    params[3].value.str="0,128,255,255";params[4].value.str="-24,-12,0,0";
    value.type=ORPHEUS_VALUE_FLOAT;value.value.f32=NAN;
    CHECK(api->set_parameter(handle,"gain_index",&value)==ORPHEUS_ERR_INVALID_ARG &&
          !memcmp(&saved,&state,sizeof(state)));
    value.value.f32=1;
    CHECK(api->set_parameter(handle,"current_gain",&value)==ORPHEUS_ERR_UNSUPPORTED);
    CHECK(api->reset(handle)==0 && state.current_gain==before && state.freeze && !state.algorithm.remaining);
    value.type=ORPHEUS_VALUE_BOOL;value.value.b=false;
    CHECK(api->set_parameter(handle,"freeze",&value)==0);
    value.type=ORPHEUS_VALUE_FLOAT;value.value.f32=0;
    CHECK(api->set_parameter(handle,"gain_index",&value)==0 && state.algorithm.remaining==1);
    ctx.frame_count=0;
    CHECK(api->process(handle,&ctx)==0 && calls==6 && !state.algorithm.remaining &&
          state.current_gain==state.algorithm.target_gain);
    ctx.frame_count=65536;out.data=input;
    CHECK(api->process(handle,&ctx)==0);
    float target=state.current_gain;
    for(size_t n=0;n<count;n++) CHECK(input[n]==(float)((int)(n%97)-48)*.03125f*target);
    CHECK(api->destroy(handle)==0);
    free(input);free(output);
    return 0;
}
