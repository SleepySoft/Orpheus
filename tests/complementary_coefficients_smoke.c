#include "component_state.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); return 1; } } while (0)
static unsigned calls;
static int emit_result;
static int emit(void* user,uint32_t port,const OrpheusDelivery* delivery) {
    (void)user;
    if(port!=1 || !delivery) return ORPHEUS_ERR_INVALID_ARG;
    calls++;
    return emit_result;
}
static int maximum_shape(const OrpheusComponentInterface* api) {
    const unsigned channels=32,elements=65536;
    const size_t count=(size_t)channels*elements;
    float* input=malloc(count*sizeof(float));float* output=malloc(count*2*sizeof(float));
    CHECK(input && output);
    for(size_t i=0;i<count;i++) input[i]=((int)(i%1025)-512)*.03125f;
    const char* ids[]={"channels","elements","total"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=channels},
        {.type=ORPHEUS_VALUE_INT,.value.i32=elements},{.type=ORPHEUS_VALUE_FLOAT,.value.f32=-.5f}};
    orpheus_builtin_complementary_coefficients_State state;void* handle=NULL;
    OrpheusConfig config={.sample_rate=48000,.block_size=8,.state_block=&state,
        .param_ids=ids,.param_values=params,.param_count=3};
    OrpheusBuffer in={.data=input,.channels=channels,.frame_count=elements,.frame_capacity=elements,
        .format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer out={.data=output,.channels=channels*2,.frame_capacity=elements,
        .format=ORPHEUS_FORMAT_F32,.interleaved=true};
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&out};
    OrpheusContinuation continuation={.emit=emit};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,
        .frame_count=8,.continuation=&continuation};
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0);
    calls=0;CHECK(api->process(handle,&ctx)==0 && calls==1 && out.frame_count==elements);
    for(unsigned row=0;row<elements;row++) {
        size_t base=(size_t)row*channels;
        CHECK(!memcmp(input+base,output+base*2,channels*sizeof(float)));
        for(unsigned channel=0;channel<channels;channel++)
            CHECK(output[base*2+channels+channel]==-.5f-input[base+channel]);
    }
    params[1].value.i32=65537;
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->destroy(handle)==0);
    free(input);free(output);
    return 0;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    CHECK(maximum_shape(api)==0);
    const uint32_t bits[]={0,0x80000000u,0x7f800000u,0xff800000u,0x7fc12345u,0x7f812345u,0x3f123456u,0xc0400000u};
    float input[8],output[16],expected[16]; memcpy(input,bits,sizeof(input));
    const char* ids[]={"channels","elements","total"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=2},
                          {.type=ORPHEUS_VALUE_INT,.value.i32=4},
                          {.type=ORPHEUS_VALUE_FLOAT,.value.f32=1.0f}};
    OrpheusConfig config={.sample_rate=48000,.block_size=1,.param_ids=ids,.param_values=params,.param_count=3};
    OrpheusBuffer in={.data=input,.channels=2,.frame_count=4,.frame_capacity=4,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer out={.data=output,.channels=4,.frame_capacity=4,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    const OrpheusBuffer* inputs[]={&in}; OrpheusBuffer* outputs[]={&out};
    OrpheusContinuation continuation={.emit=emit};
    OrpheusProcessContext context={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,.frame_count=8,
        .continuation=&continuation};
    orpheus_builtin_complementary_coefficients_State state;void* allocated=NULL;
    config.state_block=&state;CHECK(api->create(&allocated,&config)==0);
    for (unsigned kind=0;kind<2;++kind) {
        float total=kind ? -0.5f : 1.0f; params[2].value.f32=total;
        CHECK(api->prepare(&state,&config)==ORPHEUS_OK);
        for (unsigned row=0;row<4;++row) for (unsigned c=0;c<2;++c) {
            memcpy(&expected[row*4+c],&input[row*2+c],4);
            expected[row*4+2+c]=total-input[row*2+c];
        }
        calls=0;CHECK(api->process(&state,&context)==ORPHEUS_OK && out.frame_count==4 && calls==1);
        CHECK(!memcmp(output,expected,sizeof(output)));
        float before[16]; memcpy(before,output,sizeof(output));
        --in.frame_count; CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG); ++in.frame_count;
        out.frame_capacity=3; CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG); out.frame_capacity=4;
        out.channels=3; CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG); out.channels=4;
        in.interleaved=false; CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG); in.interleaved=true;
        CHECK(!memcmp(output,before,sizeof(output)));
        out.data=input; CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG); out.data=output;
        outputs[0]=NULL; CHECK(api->process(&state,&context)==ORPHEUS_OK); outputs[0]=&out;
        context.frame_count=0; calls=0;
        CHECK(api->process(&state,&context)==ORPHEUS_OK && calls==0); context.frame_count=8;
        CHECK(api->reset(&state)==ORPHEUS_OK);
        CHECK(api->process(&state,&context)==ORPHEUS_OK && !memcmp(output,expected,sizeof(output)));
        calls=0;emit_result=-77;
        CHECK(api->process(&state,&context)==-77 && calls==1);emit_result=0;
    }
    params[2].value.f32=NAN; CHECK(api->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG);
    params[2].value.f32=1.0f; params[0].value.i32=0;
    CHECK(api->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG);
    params[0].value.i32=33; CHECK(api->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG);
    config.param_count=0; config.state_block=&state;
    CHECK(api->destroy(allocated)==0);
    CHECK(api->create(&allocated,&config)==ORPHEUS_OK && allocated==&state);
    CHECK(api->prepare(allocated,&config)==ORPHEUS_OK && api->destroy(allocated)==ORPHEUS_OK);
    config.state_block=NULL;
    CHECK(api->create(&allocated,&config)==ORPHEUS_ERR_INVALID_ARG);
    return 0;
}
