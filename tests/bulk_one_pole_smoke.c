#include "orpheus_abi.h"
#include "component_state.h"
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
    orpheus_builtin_bulk_one_pole_State initial_state;
    OrpheusConfig config={.sample_rate=48000,.block_size=256,.param_ids=ids,.param_values=values,.param_count=4,
        .state_block=&initial_state};
    CHECK(api->create(&state,&config)==ORPHEUS_OK && api->prepare(state,&config)==ORPHEUS_OK);
    float data[]={-2,4,-6},result[3]={17,18,19};int32_t flag=1;
    OrpheusBuffer in={.data=data,.channels=3,.frame_count=1,.frame_capacity=1,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer update={.data=&flag,.channels=1,.frame_count=1,.frame_capacity=1,.format=ORPHEUS_FORMAT_I32,.interleaved=true};
    OrpheusBuffer out=in;out.data=result;out.frame_count=0;
    const OrpheusBuffer* inputs[]={&in,&update};OrpheusBuffer* outputs[]={&out};
    OrpheusProcessContext context={.inputs=inputs,.outputs=outputs,.input_count=2,.output_count=1,.frame_count=256};
    CHECK(api->process(state,&context)==ORPHEUS_OK && result[0]==1 && result[1]==2 && result[2]==3);
    flag=0;data[0]=NAN;CHECK(api->process(state,&context)==ORPHEUS_OK && result[0]==1);
    float saved[3];memcpy(saved,((orpheus_builtin_bulk_one_pole_State*)state)->algorithm.history,sizeof(saved));
    out.frame_capacity=0;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);out.frame_capacity=1;
    update.format=ORPHEUS_FORMAT_F32;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);update.format=ORPHEUS_FORMAT_I32;
    out.data=data;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);out.data=result;
    inputs[0]=NULL;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);inputs[0]=&in;
    context.frame_count=0;CHECK(api->process(state,&context)==ORPHEUS_OK);context.frame_count=256;
    values[2].value.f32=NAN;CHECK(api->prepare(state,&config)==ORPHEUS_ERR_INVALID_ARG);values[2].value.f32=0.5f;
    CHECK(!memcmp(saved,((orpheus_builtin_bulk_one_pole_State*)state)->algorithm.history,sizeof(saved)));
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
    CHECK(api->prepare(state,&config)==ORPHEUS_OK && api->process(state,&context)==ORPHEUS_OK);
    CHECK(result[0]==1 && result[1]==2 && result[2]==3);
    values[0].value.i32=128;values[1].value.i32=65536;
    CHECK(api->prepare(state,&config)==ORPHEUS_OK);
    size_t count=(size_t)128*65536;float* large=malloc(count*sizeof(float));float* filtered=malloc(count*sizeof(float));
    CHECK(large && filtered);for(size_t i=0;i<count;++i) large[i]=2;
    in.data=large;in.channels=128;in.frame_count=in.frame_capacity=65536;
    out.data=filtered;out.channels=128;out.frame_count=0;out.frame_capacity=65536;
    CHECK(api->process(state,&context)==ORPHEUS_OK && out.frame_count==65536);
    for(size_t i=0;i<count;++i) CHECK(filtered[i]==1);
    values[1].value.i32=65537;CHECK(api->prepare(state,&config)==ORPHEUS_ERR_INVALID_ARG);
    OrpheusValue got;CHECK(api->get_parameter(state,"elements",&got)==ORPHEUS_OK && got.value.i32==65536);
    values[1].value.i32=65536;free(large);free(filtered);
    CHECK(api->destroy(state)==ORPHEUS_OK);
    orpheus_builtin_bulk_one_pole_State external;config.state_block=&external;
    CHECK(api->create(&state,&config)==ORPHEUS_OK && state==&external && api->prepare(state,&config)==ORPHEUS_OK);
    CHECK(api->destroy(state)==ORPHEUS_OK && !external.algorithm.history);
    config.state_block=NULL;CHECK(api->create(&state,&config)==ORPHEUS_ERR_INVALID_ARG);return 0;
}
