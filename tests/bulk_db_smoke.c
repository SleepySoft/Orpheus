#include "orpheus_bulk_db.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if (!(x)) {fprintf(stderr,"%d: %s\n",__LINE__,#x);return 1;}} while (0)
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();void* state=NULL;
    const char* ids[]={"channels","elements"};
    OrpheusValue values[]={{.type=ORPHEUS_VALUE_INT,.value.i32=2},{.type=ORPHEUS_VALUE_INT,.value.i32=4}};
    OrpheusConfig config={.sample_rate=48000,.block_size=128,.param_ids=ids,.param_values=values,.param_count=2};
    CHECK(api->create(&state,&config)==ORPHEUS_OK && api->prepare(state,&config)==ORPHEUS_OK);
    float data[]={0,1,10,100,-1,NAN,INFINITY,-INFINITY},result[8];
    for (unsigned i=0;i<8;++i) result[i]=17;
    OrpheusBuffer in={.data=data,.channels=2,.frame_count=4,.frame_capacity=4,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer out=in;out.data=result;out.frame_count=19;
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&out};
    OrpheusProcessContext context={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,.frame_count=128};
    out.frame_capacity=3;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);out.frame_capacity=4;
    in.frame_count=3;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);in.frame_count=4;
    in.format=ORPHEUS_FORMAT_I32;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);in.format=ORPHEUS_FORMAT_F32;
    in.interleaved=false;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);in.interleaved=true;
    out.data=data+1;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);out.data=result;
    context.frame_count=0;CHECK(api->process(state,&context)==ORPHEUS_OK);context.frame_count=128;
    CHECK(result[0]==17 && result[7]==17 && out.frame_count==19);
    for (unsigned mode=0;mode<2;++mode) for (unsigned quantity=0;quantity<2;++quantity) {
        OrpheusValue value={.type=ORPHEUS_VALUE_INT,.value.i32=(int32_t)mode};
        CHECK(api->set_parameter(state,"mode",&value)==ORPHEUS_OK);value.value.i32=(int32_t)quantity;
        CHECK(api->set_parameter(state,"quantity",&value)==ORPHEUS_OK);
        value.type=ORPHEUS_VALUE_FLOAT;value.value.f32=0;
        CHECK(api->set_parameter(state,"epsilon",&value)==ORPHEUS_OK);
        CHECK(api->process(state,&context)==ORPHEUS_OK && out.frame_count==4);
        for (unsigned i=0;i<8;++i) {
            float factor=quantity ? 20.0f : 10.0f;
            volatile float exponent=data[i]*2.30258512f;exponent=exponent/factor;
            float expected=mode ? expf(exponent) : log10f(data[i]+0.0f)*factor;
            CHECK((isnan(expected) && isnan(result[i])) || !memcmp(&expected,result+i,4));
        }
    }
    outputs[0]=NULL;CHECK(api->process(state,&context)==ORPHEUS_OK);
    inputs[0]=NULL;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);inputs[0]=&in;outputs[0]=&out;
    values[0].value.i32=129;CHECK(api->prepare(state,&config)==ORPHEUS_ERR_INVALID_ARG);values[0].value.i32=2;
    values[1].value.i32=65537;CHECK(api->prepare(state,&config)==ORPHEUS_ERR_INVALID_ARG);values[1].value.i32=4;
    OrpheusValue value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=-1};
    CHECK(api->set_parameter(state,"epsilon",&value)==ORPHEUS_ERR_INVALID_ARG);
    value.value.f32=NAN;CHECK(api->set_parameter(state,"epsilon",&value)==ORPHEUS_ERR_INVALID_ARG);
    value.value.f32=INFINITY;CHECK(api->set_parameter(state,"epsilon",&value)==ORPHEUS_ERR_INVALID_ARG);
    value.type=ORPHEUS_VALUE_INT;value.value.i32=2;CHECK(api->set_parameter(state,"mode",&value)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->set_parameter(state,"quantity",&value)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->reset(state)==ORPHEUS_OK);
    OrpheusValue got;CHECK(api->get_parameter(state,"mode",&got)==ORPHEUS_OK && got.value.i32==1);
    CHECK(api->get_parameter(state,"quantity",&got)==ORPHEUS_OK && got.value.i32==1);
    CHECK(api->destroy(state)==ORPHEUS_OK);
    void* block=calloc(1,api->get_descriptor()->state_size);CHECK(block);config.state_block=block;
    CHECK(api->create(&state,&config)==ORPHEUS_OK && state==block && api->prepare(state,&config)==ORPHEUS_OK);
    CHECK(api->destroy(state)==ORPHEUS_OK);free(block);return 0;
}
