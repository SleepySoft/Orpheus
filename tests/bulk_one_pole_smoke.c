#include "orpheus_bulk_one_pole.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if (!(x)) {fprintf(stderr,"%d: %s\n",__LINE__,#x);return 1;}} while (0)
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();void* state=NULL;
    const char* ids[]={"channels","elements","alpha","absolute_input"};
    OrpheusValue values[]={{.type=ORPHEUS_VALUE_INT,.value.i32=3},{.type=ORPHEUS_VALUE_INT,.value.i32=1},
        {.type=ORPHEUS_VALUE_FLOAT,.value.f32=0.5f},{.type=ORPHEUS_VALUE_BOOL,.value.b=true}};
    OrpheusConfig config={.sample_rate=48000,.block_size=256,.param_ids=ids,.param_values=values,.param_count=4};
    CHECK(api->create(&state,&config)==ORPHEUS_OK && api->prepare(state,&config)==ORPHEUS_OK);
    float data[]={-2,4,-6},result[3]={17,18,19};int32_t flag=1;
    OrpheusBuffer in={.data=data,.channels=3,.frame_count=1,.frame_capacity=1,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer update={.data=&flag,.channels=1,.frame_count=1,.frame_capacity=1,.format=ORPHEUS_FORMAT_I32,.interleaved=true};
    OrpheusBuffer out=in;out.data=result;out.frame_count=17;
    const OrpheusBuffer* inputs[]={&in,&update};OrpheusBuffer* outputs[]={&out};
    OrpheusProcessContext context={.inputs=inputs,.outputs=outputs,.input_count=2,.output_count=1,.frame_count=256};
    CHECK(api->process(state,&context)==ORPHEUS_OK && result[0]==1 && result[1]==2 && result[2]==3);
    flag=0;data[0]=NAN;CHECK(api->process(state,&context)==ORPHEUS_OK && result[0]==1);
    float saved[3];memcpy(saved,((BulkOnePoleState*)state)->history,sizeof(saved));
    out.frame_capacity=0;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);out.frame_capacity=1;
    update.format=ORPHEUS_FORMAT_F32;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);update.format=ORPHEUS_FORMAT_I32;
    out.data=data;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);out.data=result;
    inputs[0]=NULL;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);inputs[0]=&in;
    context.frame_count=0;CHECK(api->process(state,&context)==ORPHEUS_OK);context.frame_count=256;
    values[2].value.f32=NAN;CHECK(api->prepare(state,&config)==ORPHEUS_ERR_INVALID_ARG);values[2].value.f32=0.5f;
    CHECK(!memcmp(saved,((BulkOnePoleState*)state)->history,sizeof(saved)));
    OrpheusValue value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=0.25f};
    CHECK(api->set_parameter(state,"alpha",&value)==ORPHEUS_OK);
    data[0]=-2;flag=1;outputs[0]=NULL;CHECK(api->process(state,&context)==ORPHEUS_OK);outputs[0]=&out;flag=0;
    CHECK(api->process(state,&context)==ORPHEUS_OK && result[0]==1.75f && result[1]==3.5f && result[2]==5.25f);
    value.type=ORPHEUS_VALUE_INT;CHECK(api->set_parameter(state,"alpha",&value)==ORPHEUS_ERR_INVALID_ARG);
    value.type=ORPHEUS_VALUE_FLOAT;value.value.f32=INFINITY;CHECK(api->set_parameter(state,"alpha",&value)==ORPHEUS_ERR_INVALID_ARG);
    value.type=ORPHEUS_VALUE_BOOL;value.value.b=false;CHECK(api->set_parameter(state,"absolute_input",&value)==ORPHEUS_OK);
    inputs[1]=NULL;CHECK(api->process(state,&context)==ORPHEUS_OK && result[0]==-1.0625f);
    CHECK(api->reset(state)==ORPHEUS_OK);flag=0;inputs[1]=&update;
    CHECK(api->process(state,&context)==ORPHEUS_OK && result[0]==0 && result[1]==0 && result[2]==0);
    value.type=ORPHEUS_VALUE_FLOAT;value.value.f32=1;CHECK(api->set_parameter(state,"alpha",&value)==ORPHEUS_OK);
    flag=1;CHECK(api->process(state,&context)==ORPHEUS_OK && result[0]==0);
    value.value.f32=0;CHECK(api->set_parameter(state,"alpha",&value)==ORPHEUS_OK);
    CHECK(api->process(state,&context)==ORPHEUS_OK && result[0]==data[0]);
    CHECK(api->destroy(state)==ORPHEUS_OK);
    config.state_block=calloc(1,api->get_descriptor()->state_size);CHECK(config.state_block);
    CHECK(api->create(&state,&config)==ORPHEUS_OK && state==config.state_block && api->prepare(state,&config)==ORPHEUS_OK);
    CHECK(api->destroy(state)==ORPHEUS_OK);free(config.state_block);return 0;
}
