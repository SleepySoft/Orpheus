#include "orpheus_abi.h"
#include "component_state.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define CHECK(x) do {if (!(x)) {fprintf(stderr,"%d: %s\n",__LINE__,#x);return 1;}} while (0)
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();orpheus_builtin_bulk_subtract_State state;
    const char* ids[]={"channels","elements"};
    OrpheusValue values[]={{.type=ORPHEUS_VALUE_INT,.value.i32=2},{.type=ORPHEUS_VALUE_INT,.value.i32=5}};
    OrpheusConfig config={.sample_rate=48000,.block_size=1,.param_ids=ids,.param_values=values,.param_count=2,
        .state_block=&state};
    void* instance=NULL;
    CHECK(api->create(&instance,&config)==ORPHEUS_OK && instance==&state);
    CHECK(api->prepare(instance,&config)==ORPHEUS_OK);
    float input[2][10],output[10],expected[10];
    OrpheusBuffer buffers[2];
    for(unsigned p=0;p<2;++p) buffers[p]=(OrpheusBuffer){.data=input[p],.channels=2,.frame_count=5,.frame_capacity=5,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer out={.data=output,.channels=2,.frame_capacity=5,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    const OrpheusBuffer* inputs[]={buffers,buffers+1};OrpheusBuffer* outputs[]={&out};
    OrpheusProcessContext context={.inputs=inputs,.outputs=outputs,.input_count=2,.output_count=1,.frame_count=1};
    const uint32_t bits[]={0,0x80000000u,0x7f800000u,0xff800000u,0x7fc12345u,0x7f812345u,1,0x7f7fffffu,0x3f800000u,0xbf800000u};
    for(unsigned call=0;call<10;++call) {
        for(unsigned p=0;p<2;++p) for(unsigned i=0;i<10;++i) memcpy(input[p]+i,&bits[(i+call*p)%10],4);
        for(unsigned i=0;i<10;++i) expected[i]=input[0][i]-input[1][i];
        CHECK(api->process(&state,&context)==ORPHEUS_OK && out.frame_count==5 && !memcmp(output,expected,sizeof(output)));
        CHECK(api->reset(&state)==ORPHEUS_OK && state.channels==2 && state.elements==5);
    }
    memset(output,0x5a,sizeof(output));float saved[10];memcpy(saved,output,sizeof(saved));
    buffers[1].frame_capacity=4;
    CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG && !memcmp(saved,output,sizeof(saved)));
    buffers[1].frame_capacity=5;buffers[1].format=ORPHEUS_FORMAT_I32;
    CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG && !memcmp(saved,output,sizeof(saved)));
    buffers[1].format=ORPHEUS_FORMAT_F32;inputs[1]=NULL;
    CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG && !memcmp(saved,output,sizeof(saved)));
    inputs[1]=buffers+1;out.data=input[0]+1;
    CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG);out.data=output;
    outputs[0]=NULL;CHECK(api->process(&state,&context)==ORPHEUS_OK);
    outputs[0]=&out;context.frame_count=0;
    CHECK(api->process(&state,&context)==ORPHEUS_OK && !memcmp(saved,output,sizeof(saved)));
    orpheus_builtin_bulk_subtract_State previous=state;values[1].value.i32=0;
    CHECK(api->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&previous,&state,sizeof(state)));
    values[0].value.i32=64;values[1].value.i32=65536;
    CHECK(api->prepare(&state,&config)==ORPHEUS_OK);
    size_t count=(size_t)64*65536;
    float* large_a=malloc(count*sizeof(float));float* large_b=malloc(count*sizeof(float));float* difference=malloc(count*sizeof(float));
    CHECK(large_a && large_b && difference);
    for(size_t i=0;i<count;++i) { large_a[i]=2;large_b[i]=0.5f; }
    for(unsigned p=0;p<2;++p) {
        buffers[p].data=p ? large_b : large_a;buffers[p].channels=64;
        buffers[p].frame_count=buffers[p].frame_capacity=65536;inputs[p]=buffers+p;
    }
    out.data=difference;out.channels=64;out.frame_count=0;out.frame_capacity=65536;context.frame_count=128;
    CHECK(api->process(&state,&context)==ORPHEUS_OK && out.frame_count==65536);
    for(size_t i=0;i<count;++i) CHECK(difference[i]==1.5f);
    values[1].value.i32=65537;CHECK(api->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG);
    OrpheusValue got;CHECK(api->get_parameter(&state,"elements",&got)==ORPHEUS_OK && got.value.i32==65536);
    free(large_a);free(large_b);free(difference);
    CHECK(api->destroy(&state)==ORPHEUS_OK);
    void* allocated=NULL;config.state_block=&state;values[1].value.i32=5;
    CHECK(api->create(&allocated,&config)==ORPHEUS_OK && allocated==&state);
    CHECK(api->destroy(allocated)==ORPHEUS_OK);config.state_block=NULL;
    CHECK(api->create(&allocated,&config)==ORPHEUS_ERR_INVALID_ARG);return 0;
}
