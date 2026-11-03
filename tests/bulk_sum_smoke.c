#include "orpheus_bulk_sum.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if (!(x)) {fprintf(stderr,"%d: %s\n",__LINE__,#x);return 1;}} while (0)
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();void* state=NULL;
    const char* ids[]={"channels","elements"};
    OrpheusValue values[]={{.type=ORPHEUS_VALUE_INT,.value.i32=2},{.type=ORPHEUS_VALUE_INT,.value.i32=3}};
    OrpheusConfig config={.sample_rate=48000,.block_size=128,.param_ids=ids,.param_values=values,.param_count=2};
    CHECK(api->create(&state,&config)==ORPHEUS_OK && api->prepare(state,&config)==ORPHEUS_OK);
    float data[]={1e20f,-1e20f,-1e20f,1e20f,1,-1},result[2]={17,18};
    OrpheusBuffer in={.data=data,.channels=2,.frame_count=3,.frame_capacity=3,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer out={.data=result,.channels=2,.frame_count=17,.frame_capacity=1,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&out};
    OrpheusProcessContext context={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,.frame_count=128};
    out.frame_capacity=0;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);out.frame_capacity=1;
    in.frame_count=2;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);in.frame_count=3;
    in.format=ORPHEUS_FORMAT_I32;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);in.format=ORPHEUS_FORMAT_F32;
    out.data=data+1;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);out.data=result;
    inputs[0]=NULL;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);inputs[0]=&in;
    context.frame_count=0;CHECK(api->process(state,&context)==ORPHEUS_OK);context.frame_count=128;
    CHECK(result[0]==17 && result[1]==18 && out.frame_count==17);
    CHECK(api->process(state,&context)==ORPHEUS_OK && result[0]==1 && result[1]==-1 && out.frame_count==1);
    CHECK(api->reset(state)==ORPHEUS_OK && api->process(state,&context)==ORPHEUS_OK && result[0]==1 && result[1]==-1);
    outputs[0]=NULL;CHECK(api->process(state,&context)==ORPHEUS_OK);
    inputs[0]=NULL;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);inputs[0]=&in;outputs[0]=&out;
    values[0].value.i32=129;CHECK(api->prepare(state,&config)==ORPHEUS_ERR_INVALID_ARG);values[0].value.i32=2;
    values[1].value.i32=0;CHECK(api->prepare(state,&config)==ORPHEUS_ERR_INVALID_ARG);values[1].value.i32=3;
    values[1].type=ORPHEUS_VALUE_FLOAT;CHECK(api->prepare(state,&config)==ORPHEUS_ERR_INVALID_ARG);values[1].type=ORPHEUS_VALUE_INT;
    CHECK(api->process(state,&context)==ORPHEUS_OK && result[0]==1);
    values[1].value.i32=1;CHECK(api->prepare(state,&config)==ORPHEUS_OK);in.frame_count=1;
    uint32_t bits[]={0x80000000u,0x7f812345u},actual[2];memcpy(data,bits,8);
    CHECK(api->process(state,&context)==ORPHEUS_OK);memcpy(actual,result,8);CHECK(!memcmp(actual,bits,8));
    values[1].value.i32=3;CHECK(api->prepare(state,&config)==ORPHEUS_OK);in.frame_count=3;
    data[0]=INFINITY;data[2]=-INFINITY;data[4]=1;
    CHECK(api->process(state,&context)==ORPHEUS_OK && isnan(result[0]));
    OrpheusValue got;CHECK(api->get_parameter(state,"elements",&got)==ORPHEUS_OK && got.value.i32==3);
    CHECK(api->destroy(state)==ORPHEUS_OK);
    void* block=calloc(1,api->get_descriptor()->state_size);CHECK(block);config.state_block=block;
    CHECK(api->create(&state,&config)==ORPHEUS_OK && state==block && api->prepare(state,&config)==ORPHEUS_OK);
    CHECK(api->destroy(state)==ORPHEUS_OK);free(block);return 0;
}
