#include "orpheus_abi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if (!(x)) {fprintf(stderr,"%d: %s\n",__LINE__,#x);return 1;}} while (0)
const OrpheusComponentInterface* native_bulk_join(void);
const OrpheusComponentInterface* native_bulk_split(void);
int main(void) {
    const OrpheusComponentInterface* join=native_bulk_join();const OrpheusComponentInterface* split=native_bulk_split();
    const char* ids[]={"channels","elements"};
    OrpheusValue values[]={{.type=ORPHEUS_VALUE_INT,.value.i32=3},{.type=ORPHEUS_VALUE_INT,.value.i32=5}};
    OrpheusConfig config={.sample_rate=48000,.block_size=128,.param_ids=ids,.param_values=values,.param_count=2};
    void* states[2]={NULL,NULL};const OrpheusComponentInterface* apis[]={join,split};
    for(unsigned i=0;i<2;++i) {
        CHECK(apis[i]->create(states+i,&config)==ORPHEUS_OK);
        CHECK(apis[i]->prepare(states[i],&config)==ORPHEUS_OK);
    }
    uint32_t bus[15],packed[15],mono[3][5],want[15];
    const uint32_t bits[]={0,0x80000000u,0x7f800000u,0xff800000u,0x7fc12345u,0x7f812345u,1,0x3f99999au};
    for(unsigned i=0;i<15;++i) bus[i]=bits[i%8];
    OrpheusBuffer in={.data=bus,.channels=3,.frame_count=5,.frame_capacity=5,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer out={.data=packed,.channels=3,.frame_capacity=5,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer m[3];
    for(unsigned i=0;i<3;++i) m[i]=(OrpheusBuffer){.data=mono[i],.channels=1,.frame_capacity=5,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    const OrpheusBuffer* ins[]={&in};OrpheusBuffer* outs[]={m,m+1,m+2};
    OrpheusProcessContext context={.inputs=ins,.outputs=outs,.input_count=1,.output_count=3,.frame_count=128};
    memset(mono,0x5a,sizeof(mono));m[2].frame_capacity=4;
    CHECK(split->process(states[1],&context)==ORPHEUS_ERR_INVALID_ARG);
    for(unsigned i=0;i<sizeof(mono);++i) CHECK(((unsigned char*)mono)[i]==0x5a);
    m[2].frame_capacity=5;m[2].data=mono[0];
    CHECK(split->process(states[1],&context)==ORPHEUS_ERR_INVALID_ARG);m[2].data=mono[2];
    m[1].data=bus;CHECK(split->process(states[1],&context)==ORPHEUS_ERR_INVALID_ARG);m[1].data=mono[1];
    context.frame_count=0;CHECK(split->process(states[1],&context)==ORPHEUS_OK);
    for(unsigned i=0;i<sizeof(mono);++i) CHECK(((unsigned char*)mono)[i]==0x5a);
    context.frame_count=128;CHECK(split->process(states[1],&context)==ORPHEUS_OK);
    for(unsigned c=0;c<3;++c) {
        CHECK(m[c].frame_count==5);
        for(unsigned i=0;i<5;++i) CHECK(mono[c][i]==bus[i*3+c]);
    }
    outs[1]=NULL;CHECK(split->process(states[1],&context)==ORPHEUS_OK);outs[1]=m+1;
    const OrpheusBuffer* branches[]={m+2,NULL,m};OrpheusBuffer* output[]={&out};
    context.inputs=branches;context.input_count=3;context.outputs=output;context.output_count=1;
    out.frame_capacity=4;memset(packed,0x5a,sizeof(packed));
    CHECK(join->process(states[0],&context)==ORPHEUS_ERR_INVALID_ARG);out.frame_capacity=5;
    out.data=mono[0];CHECK(join->process(states[0],&context)==ORPHEUS_ERR_INVALID_ARG);out.data=packed;
    m[2].format=ORPHEUS_FORMAT_I32;CHECK(join->process(states[0],&context)==ORPHEUS_ERR_INVALID_ARG);m[2].format=ORPHEUS_FORMAT_F32;
    context.frame_count=0;CHECK(join->process(states[0],&context)==ORPHEUS_OK);
    for(unsigned i=0;i<sizeof(packed);++i) CHECK(((unsigned char*)packed)[i]==0x5a);
    context.frame_count=128;CHECK(join->process(states[0],&context)==ORPHEUS_OK);
    for(unsigned i=0;i<5;++i) {want[i*3]=bus[i*3+2];want[i*3+1]=0;want[i*3+2]=bus[i*3];}
    CHECK(out.frame_count==5 && !memcmp(want,packed,sizeof(want)));
    output[0]=NULL;CHECK(join->process(states[0],&context)==ORPHEUS_OK);output[0]=&out;
    for(unsigned i=0;i<2;++i) {
        values[0].value.i32=0;CHECK(apis[i]->prepare(states[i],&config)==ORPHEUS_ERR_INVALID_ARG);values[0].value.i32=3;
        OrpheusValue got;CHECK(apis[i]->get_parameter(states[i],"channels",&got)==ORPHEUS_OK && got.value.i32==3);
        CHECK(apis[i]->reset(states[i])==ORPHEUS_OK);
        CHECK(apis[i]->destroy(states[i])==ORPHEUS_OK);
        void* block=calloc(1,apis[i]->get_descriptor()->state_size);CHECK(block);config.state_block=block;
        CHECK(apis[i]->create(states+i,&config)==ORPHEUS_OK && states[i]==block);
        CHECK(apis[i]->prepare(states[i],&config)==ORPHEUS_OK && apis[i]->destroy(states[i])==ORPHEUS_OK);
        free(block);config.state_block=NULL;
    }
    return 0;
}
