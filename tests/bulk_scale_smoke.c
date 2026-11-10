#include "orpheus_abi.h"
#include "component_state.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if (!(x)) {fprintf(stderr,"%d: %s\n",__LINE__,#x);return 1;}} while (0)
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();void* state=NULL;
    const char* ids[]={"channels","elements"};
    OrpheusValue values[]={{.type=ORPHEUS_VALUE_INT,.value.i32=2},{.type=ORPHEUS_VALUE_INT,.value.i32=3}};
    orpheus_builtin_bulk_scale_State initial_state;
    OrpheusConfig config={.sample_rate=48000,.block_size=128,.param_ids=ids,.param_values=values,.param_count=2,
        .state_block=&initial_state};
    CHECK(api->create(&state,&config)==ORPHEUS_OK && api->prepare(state,&config)==ORPHEUS_OK);
    float data[]={-1,2,-0.0f,NAN,INFINITY,-INFINITY},result[6],factor=2;int32_t multiplier=0;
    for (unsigned i=0;i<6;++i) result[i]=17;
    OrpheusBuffer in={.data=data,.channels=2,.frame_count=3,.frame_capacity=3,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer out=in;out.data=result;out.frame_count=0;
    OrpheusBuffer scalar={.data=&factor,.channels=1,.frame_count=1,.frame_capacity=1,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer integer=scalar;integer.data=&multiplier;integer.format=ORPHEUS_FORMAT_I32;
    const OrpheusBuffer* inputs[]={&in,&scalar,NULL};OrpheusBuffer* outputs[]={&out};
    OrpheusProcessContext context={.inputs=inputs,.outputs=outputs,.input_count=3,.output_count=1,.frame_count=128};
    inputs[2]=&integer;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);inputs[2]=NULL;
    inputs[1]=NULL;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);inputs[1]=&scalar;
    out.frame_capacity=2;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);out.frame_capacity=3;
    in.frame_count=2;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);in.frame_count=3;
    scalar.format=ORPHEUS_FORMAT_I32;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);scalar.format=ORPHEUS_FORMAT_F32;
    scalar.frame_count=0;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);scalar.frame_count=1;
    out.data=data+1;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);out.data=result;
    scalar.data=result;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);scalar.data=&factor;
    context.frame_count=0;CHECK(api->process(state,&context)==ORPHEUS_OK);context.frame_count=128;
    CHECK(result[0]==17 && result[5]==17 && out.frame_count==3);
    CHECK(api->process(state,&context)==ORPHEUS_OK && result[0]==-2 && result[1]==4 && isnan(result[3]));
    inputs[1]=NULL;inputs[2]=&integer;
    CHECK(api->process(state,&context)==ORPHEUS_OK && result[0]==0 && signbit(result[0]) &&
          result[1]==0 && !signbit(result[1]) && isnan(result[3]) && isnan(result[4]));
    multiplier=-2;CHECK(api->process(state,&context)==ORPHEUS_OK && result[0]==2 && result[1]==-4);
    multiplier=16777217;data[0]=1;
    CHECK(api->process(state,&context)==ORPHEUS_OK && result[0]==16777216.0f);
    outputs[0]=NULL;CHECK(api->process(state,&context)==ORPHEUS_OK);
    integer.format=ORPHEUS_FORMAT_F32;CHECK(api->process(state,&context)==ORPHEUS_ERR_INVALID_ARG);integer.format=ORPHEUS_FORMAT_I32;
    outputs[0]=&out;CHECK(api->reset(state)==ORPHEUS_OK && api->process(state,&context)==ORPHEUS_OK);
    values[0].value.i32=129;CHECK(api->prepare(state,&config)==ORPHEUS_ERR_INVALID_ARG);values[0].value.i32=2;
    values[1].value.i32=0;CHECK(api->prepare(state,&config)==ORPHEUS_ERR_INVALID_ARG);values[1].value.i32=3;
    CHECK(api->process(state,&context)==ORPHEUS_OK && result[0]==16777216.0f);
    OrpheusValue got;CHECK(api->get_parameter(state,"elements",&got)==ORPHEUS_OK && got.value.i32==3);
    values[0].value.i32=128;values[1].value.i32=65536;
    CHECK(api->prepare(state,&config)==ORPHEUS_OK);
    size_t count=(size_t)128*65536;float* large=malloc(count*sizeof(float));float* scaled=malloc(count*sizeof(float));
    CHECK(large && scaled);for(size_t i=0;i<count;++i) large[i]=1;
    float two=2;
    OrpheusBuffer large_factor={.data=&two,.channels=1,.frame_count=1,.frame_capacity=1,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    inputs[1]=&large_factor;inputs[2]=NULL;
    in.data=large;in.channels=128;in.frame_count=in.frame_capacity=65536;
    out.data=scaled;out.channels=128;out.frame_count=0;out.frame_capacity=65536;
    CHECK(api->process(state,&context)==ORPHEUS_OK && out.frame_count==65536);
    for(size_t i=0;i<count;++i) CHECK(scaled[i]==2);
    values[1].value.i32=65537;CHECK(api->prepare(state,&config)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->get_parameter(state,"elements",&got)==ORPHEUS_OK && got.value.i32==65536);
    values[1].value.i32=65536;free(large);free(scaled);
    CHECK(api->destroy(state)==ORPHEUS_OK);
    orpheus_builtin_bulk_scale_State second_state;config.state_block=&second_state;
    CHECK(api->create(&state,&config)==ORPHEUS_OK && state==&second_state && api->prepare(state,&config)==ORPHEUS_OK);
    CHECK(api->destroy(state)==ORPHEUS_OK);config.state_block=NULL;
    CHECK(api->create(&state,&config)==ORPHEUS_ERR_INVALID_ARG);return 0;
}
