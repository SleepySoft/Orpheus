#include "component_state.h"
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
    const uint32_t patterns[]={0x80000000u,0x7fa12345u,0x7fc76543u,1,0x7f800000u,0xff800000u,0x3f800000u};
    for(size_t i=0;i<(size_t)frames*cin;i++) memcpy(input+i,patterns+i%7,4);
    const char* ids[]={"channels_in","channels_out"};
    OrpheusValue values[]={{.type=ORPHEUS_VALUE_INT,.value.i32=cin},{.type=ORPHEUS_VALUE_INT,.value.i32=cout}};
    orpheus_builtin_input_select_State state;
    OrpheusConfig config={.sample_rate=48000,.block_size=frames,.param_count=2,.param_ids=ids,
        .param_values=values,.state_block=&state};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0);
    CHECK(state.select[0]==0 && state.override_select[0]==0);
    OrpheusBuffer in={.data=input,.format=ORPHEUS_FORMAT_F32,.channels=cin,
        .frame_capacity=frames,.frame_count=frames,.interleaved=true};
    OrpheusBuffer out=in;out.data=output;out.channels=cout;
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
            uint32_t expected=c<cin ? patterns[((size_t)n*cin+c)%7] : 0;
            CHECK(!memcmp(output+(size_t)n*cout+c,&expected,4));
        }
    }
    in.frame_count=frames-1;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && capture.calls==2);
    in.frame_count=frames;out.data=input;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && capture.calls==2);
    out.data=output;inputs[0]=NULL;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && capture.calls==2);
    outputs[0]=NULL;ctx.frame_count=0;
    CHECK(api->process(handle,&ctx)==0 && capture.calls==2);
    CHECK(api->destroy(handle)==0);
    free(input);free(output);
    return 0;
}

static int transactions(const OrpheusComponentInterface* api) {
    const char* ids[]={"channels_in","channels_out","select","override_select"};
    OrpheusValue values[]={{.type=ORPHEUS_VALUE_INT,.value.i32=4},
        {.type=ORPHEUS_VALUE_INT,.value.i32=4},{.type=ORPHEUS_VALUE_STRING,.value.str="4,2"},
        {.type=ORPHEUS_VALUE_STRING,.value.str="0,4,99,-1"}};
    orpheus_builtin_input_select_State state;
    OrpheusConfig config={.block_size=7,.param_count=4,.param_ids=ids,.param_values=values,.state_block=&state};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0);
    const int32_t initial[]={3,3,-1,-1};
    CHECK(!memcmp(state.algorithm.map,initial,sizeof(initial)));
    const char* invalid[]={"x","1x","1,,2","1,","1, ","1 2","1,2,3,4,5","2147483648","-2147483649"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++) {
        orpheus_builtin_input_select_State saved=state;
        OrpheusValue value={.type=ORPHEUS_VALUE_STRING,.value.str=invalid[i]};
        CHECK(api->set_parameter(handle,"override_select",&value)==ORPHEUS_ERR_INVALID_ARG);
        CHECK(!memcmp(&state,&saved,sizeof(state)));
        values[2].value.str=invalid[i];
        CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG);
        CHECK(!memcmp(&state,&saved,sizeof(state)));
    }
    values[2].value.str="4,2";
    OrpheusValue override={.type=ORPHEUS_VALUE_STRING,.value.str=" \t0, 1 "};
    CHECK(api->set_parameter(handle,"override_select",&override)==0);
    const int32_t updated[]={3,0,2,3};
    CHECK(!memcmp(state.algorithm.map,updated,sizeof(updated)));
    CHECK(api->reset(handle)==0 && !memcmp(state.algorithm.map,updated,sizeof(updated)));
    OrpheusValue readback;
    CHECK(api->get_parameter(handle,"override_select",&readback)==0 && !strcmp(readback.value.str," \t0, 1 "));
    override.value.str="";
    CHECK(api->set_parameter(handle,"override_select",&override)==0);
    const int32_t base[]={3,1,2,3};
    CHECK(!memcmp(state.algorithm.map,base,sizeof(base)));
    char boundary[257];memset(boundary,' ',256);boundary[255]=0;override.value.str=boundary;
    CHECK(api->set_parameter(handle,"override_select",&override)==0);
    boundary[255]=' ';boundary[256]=0;
    CHECK(api->set_parameter(handle,"override_select",&override)==ORPHEUS_ERR_INVALID_ARG);
    override.type=ORPHEUS_VALUE_INT;
    CHECK(api->set_parameter(handle,"override_select",&override)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->destroy(handle)==0);
    return 0;
}

int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    CHECK(transactions(api)==0);
    CHECK(shape(api,1,7,1)==0);
    CHECK(shape(api,3,1,17)==0);
    CHECK(shape(api,32,32,65536)==0);
    return 0;
}
