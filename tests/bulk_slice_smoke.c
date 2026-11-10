#include "orpheus_abi.h"
#include "component_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if (!(x)) {fprintf(stderr,"%d: %s\n",__LINE__,#x);return 1;}} while (0)
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();void* state=NULL;
    const char* ids[]={"input_channels","input_elements","channels","elements","offset","stride"};
    OrpheusValue values[6];
    const int32_t initial[]={3,5,2,3,2,2};
    for(unsigned p=0;p<6;++p) values[p]=(OrpheusValue){.type=ORPHEUS_VALUE_INT,.value.i32=initial[p]};
    orpheus_builtin_bulk_slice_State initial_state;
    OrpheusConfig config={.sample_rate=48000,.block_size=32,.param_ids=ids,.param_values=values,.param_count=6,
        .state_block=&initial_state};
    CHECK(api->create(&state,&config)==ORPHEUS_OK && api->prepare(state,&config)==ORPHEUS_OK);
    uint32_t source[15],output[6];
    const uint32_t bits[]={0,0x80000000u,0x7f800000u,0xff800000u,0x7fc12345u,0x7f812345u,1,0x3f99999au};
    for(unsigned i=0;i<15;++i) source[i]=bits[i%8];
    OrpheusBuffer in={.data=source,.channels=3,.frame_count=5,.frame_capacity=5,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer out={.data=output,.channels=2,.frame_count=17,.frame_capacity=3,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&out};
    OrpheusProcessContext context={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,.frame_count=32};
    memset(output,0x5a,sizeof(output));out.frame_capacity=2;
    CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG && out.frame_count==17);
    out.frame_capacity=3;out.frame_count=0;out.data=source+1;
    CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);out.data=output;
    in.format=ORPHEUS_FORMAT_I32;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);in.format=ORPHEUS_FORMAT_F32;
    out.interleaved=false;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);out.interleaved=true;
    in.frame_count=4;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);in.frame_count=5;
    inputs[0]=NULL;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);inputs[0]=&in;
    context.frame_count=0;CHECK(api->process(state,&context)==ORPHEUS_OK);context.frame_count=32;
    for(unsigned i=0;i<sizeof(output);++i) CHECK(((unsigned char*)output)[i]==0x5a);
    CHECK(api->process(state,&context)==ORPHEUS_OK && out.frame_count==3);
    for(unsigned i=0;i<6;++i) CHECK(output[i]==source[2+i*2]);
    outputs[0]=NULL;CHECK(api->process(state,&context)==ORPHEUS_OK);
    inputs[0]=NULL;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);inputs[0]=&in;outputs[0]=&out;
    for(unsigned p=0;p<6;++p) {
        values[p].value.i32=p==4 ? -1 : 0;
        CHECK(api->prepare(state,&config)==ORPHEUS_ERR_INVALID_ARG);values[p].value.i32=initial[p];
        OrpheusValue got;CHECK(api->get_parameter(state,ids[p],&got)==ORPHEUS_OK && got.value.i32==initial[p]);
    }
    values[5].value.i32=2147483647;CHECK(api->prepare(state,&config)==ORPHEUS_ERR_INVALID_ARG);values[5].value.i32=2;
    values[4].value.i32=5;CHECK(api->prepare(state,&config)==ORPHEUS_ERR_INVALID_ARG);values[4].value.i32=2;
    values[0].type=ORPHEUS_VALUE_FLOAT;CHECK(api->prepare(state,&config)==ORPHEUS_ERR_INVALID_ARG);values[0].type=ORPHEUS_VALUE_INT;
    ids[0]="unknown";CHECK(api->prepare(state,&config)==ORPHEUS_ERR_NOT_FOUND);ids[0]="input_channels";
    CHECK(api->reset(state)==ORPHEUS_OK && api->process(state,&context)==ORPHEUS_OK);
    for(unsigned i=0;i<6;++i) CHECK(output[i]==source[2+i*2]);
    const int32_t wide[]={100,130,1,65,99,200};
    for(unsigned p=0;p<6;++p) values[p].value.i32=wide[p];
    CHECK(api->prepare(state,&config)==ORPHEUS_OK);
    uint32_t matrix[13000],diagonal[65];
    for(unsigned i=0;i<13000;++i) matrix[i]=bits[i%8]^(i<<8);
    in.data=matrix;in.channels=100;in.frame_count=in.frame_capacity=130;
    out.data=diagonal;out.channels=1;out.frame_capacity=65;
    CHECK(api->process(state,&context)==ORPHEUS_OK && out.frame_count==65);
    for(unsigned i=0;i<65;++i) CHECK(diagonal[i]==matrix[99+i*200]);
    values[3].value.i32=1;values[4].value.i32=12999;values[5].value.i32=2147483647;
    CHECK(api->prepare(state,&config)==ORPHEUS_OK && api->process(state,&context)==ORPHEUS_OK && diagonal[0]==matrix[12999]);
    OrpheusValue invalid={.type=ORPHEUS_VALUE_INT,.value.i32=13000},got;
    CHECK(api->set_parameter(state,"offset",&invalid)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->get_parameter(state,"offset",&got)==ORPHEUS_OK && got.value.i32==12999);
    CHECK(api->process(state,&context)==ORPHEUS_OK && diagonal[0]==matrix[12999]);
    CHECK(api->destroy(state)==ORPHEUS_OK);
    void* block=calloc(1,api->get_descriptor()->state_size);CHECK(block);config.state_block=block;
    CHECK(api->create(&state,&config)==ORPHEUS_OK && state==block && api->prepare(state,&config)==ORPHEUS_OK);
    CHECK(api->destroy(state)==ORPHEUS_OK);free(block);
    return 0;
}
