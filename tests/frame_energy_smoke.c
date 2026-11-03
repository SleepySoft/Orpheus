#include "orpheus_frame_energy.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if (!(x)) {fprintf(stderr,"%d: %s\n",__LINE__,#x);return 1;}} while (0)
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();void* state=NULL;
    const char* ids[]={"channels","frames"};
    OrpheusValue values[]={{.type=ORPHEUS_VALUE_INT,.value.i32=2},{.type=ORPHEUS_VALUE_INT,.value.i32=3}};
    OrpheusConfig config={.sample_rate=48000,.block_size=128,.param_ids=ids,.param_values=values,.param_count=2};
    CHECK(api->create(&state,&config)==ORPHEUS_OK && api->prepare(state,&config)==ORPHEUS_OK);
    float data[]={1,2,3,4,5,6},result=17;
    OrpheusBuffer in={.data=data,.channels=2,.frame_count=3,.frame_capacity=3,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer out={.data=&result,.channels=1,.frame_count=19,.frame_capacity=1,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&out};
    OrpheusProcessContext context={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,.frame_count=128};
    out.frame_capacity=0;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);out.frame_capacity=1;
    in.frame_count=2;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);in.frame_count=3;
    in.frame_capacity=2;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);in.frame_capacity=3;
    in.channels=1;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);in.channels=2;
    in.interleaved=false;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);in.interleaved=true;
    in.format=ORPHEUS_FORMAT_I32;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);in.format=ORPHEUS_FORMAT_F32;
    out.format=ORPHEUS_FORMAT_I32;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);out.format=ORPHEUS_FORMAT_F32;
    out.data=data+1;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);out.data=&result;
    inputs[0]=NULL;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);inputs[0]=&in;
    context.frame_count=0;CHECK(api->process(state,&context)==ORPHEUS_OK);context.frame_count=128;
    CHECK(result==17 && out.frame_count==19);
    CHECK(api->process(state,&context)==ORPHEUS_OK && result==91 && out.frame_count==1);
    CHECK(api->process(state,&context)==ORPHEUS_OK && result==91);
    CHECK(api->reset(state)==ORPHEUS_OK && api->process(state,&context)==ORPHEUS_OK && result==91);
    outputs[0]=NULL;CHECK(api->process(state,&context)==ORPHEUS_OK);
    inputs[0]=NULL;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);inputs[0]=&in;outputs[0]=&out;
    values[0].value.i32=129;CHECK(api->prepare(state,&config)==ORPHEUS_ERR_INVALID_ARG);values[0].value.i32=2;
    values[1].value.i32=0;CHECK(api->prepare(state,&config)==ORPHEUS_ERR_INVALID_ARG);values[1].value.i32=3;
    values[1].type=ORPHEUS_VALUE_FLOAT;CHECK(api->prepare(state,&config)==ORPHEUS_ERR_INVALID_ARG);values[1].type=ORPHEUS_VALUE_INT;
    ids[1]="unknown";CHECK(api->prepare(state,&config)==ORPHEUS_ERR_INVALID_ARG);ids[1]="frames";
    CHECK(api->process(state,&context)==ORPHEUS_OK && result==91);
    memset(data,0,sizeof(data));CHECK(api->process(state,&context)==ORPHEUS_OK && result==0 && !signbit(result));
    data[0]=NAN;CHECK(api->process(state,&context)==ORPHEUS_OK && isnan(result));
    data[0]=INFINITY;CHECK(api->process(state,&context)==ORPHEUS_OK && isinf(result));
    data[0]=-0.0f;data[1]=-0.0f;CHECK(api->process(state,&context)==ORPHEUS_OK && result==0 && !signbit(result));
    OrpheusValue got;CHECK(api->get_parameter(state,"frames",&got)==ORPHEUS_OK && got.value.i32==3);
    values[0].value.i32=128;values[1].value.i32=1;CHECK(api->prepare(state,&config)==ORPHEUS_OK);
    float wide[128];for (unsigned c=0;c<128;++c) wide[c]=1;
    in.data=wide;in.channels=128;in.frame_count=in.frame_capacity=1;
    CHECK(api->process(state,&context)==ORPHEUS_OK && result==128);
    CHECK(api->destroy(state)==ORPHEUS_OK);
    void* block=calloc(1,api->get_descriptor()->state_size);CHECK(block);config.state_block=block;
    CHECK(api->create(&state,&config)==ORPHEUS_OK && state==block && api->prepare(state,&config)==ORPHEUS_OK);
    CHECK(api->process(state,&context)==ORPHEUS_OK && result==128);
    CHECK(api->destroy(state)==ORPHEUS_OK);free(block);return 0;
}
