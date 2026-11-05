#include "orpheus_host_audio.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>
#define CHECK(x) do {if (!(x)) {fprintf(stderr,"%d: %s\n",__LINE__,#x);return 1;}} while(0)
extern const OrpheusComponentInterface* orpheus_get_interface(void);
static int calls;
static int next(void* user,uint32_t branch) {(void)user;++calls;return branch==UINT32_MAX ? -77 : -99;}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    HostAudioState external;void* state=NULL;
    const char* ids[]={"channels","mode"};
    OrpheusValue values[]={{.type=ORPHEUS_VALUE_INT,.value.i32=3},{.type=ORPHEUS_VALUE_INT,.value.i32=0}};
    OrpheusConfig config={.block_size=4,.channels=3,.state_block=&external,
        .param_count=2,.param_ids=ids,.param_values=values};
    CHECK(api->create(&state,&config)==0 && state==&external && api->prepare(state,&config)==0);
    float data[12]={0};
    OrpheusBuffer output={.data=data,.channels=3,.frame_capacity=4,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    const OrpheusBuffer* inputs[]={NULL};OrpheusBuffer* outputs[]={&output};
    OrpheusContinuation continuation={.call=next,.count=1};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,
        .frame_count=4,.continuation=&continuation};
    CHECK(api->process(state,&ctx)==ORPHEUS_ERR_PROCESSING && calls==0 && external.missing_blocks==1);
    external.missing_blocks=INT_MAX;
    CHECK(api->process(state,&ctx)==ORPHEUS_ERR_PROCESSING && external.missing_blocks==INT_MAX);
    for (unsigned i=0;i<12;++i) external.data[i]=(float)i-3.5f;
    OrpheusValue ready={.type=ORPHEUS_VALUE_BOOL,.value.b=true};
    CHECK(api->set_parameter(state,"ready",&ready)==0);
    output.frame_capacity=3;CHECK(api->process(state,&ctx)!=0 && external.ready);output.frame_capacity=4;
    output.channels=2;CHECK(api->process(state,&ctx)!=0 && external.ready);output.channels=3;
    output.data=external.data+1;CHECK(api->process(state,&ctx)!=0 && external.ready);output.data=data;
    output.format=ORPHEUS_FORMAT_I32;CHECK(api->process(state,&ctx)!=0 && external.ready);output.format=ORPHEUS_FORMAT_F32;
    CHECK(api->process(state,&ctx)==-77 && calls==1 && !external.ready && output.frame_count==4);
    for (unsigned i=0;i<12;++i) CHECK(data[i]==(float)i-3.5f);
    outputs[0]=NULL;CHECK(api->set_parameter(state,"ready",&ready)==0);
    CHECK(api->process(state,&ctx)==-77 && !external.ready);outputs[0]=&output;
    values[0].value.i32=65;CHECK(api->prepare(state,&config)!=0 && external.channels==3);
    values[0].value.i32=3;values[1].value.i32=1;
    CHECK(api->prepare(state,&config)==0 && api->set_parameter(state,"ready",&ready)!=0);
    OrpheusBuffer input=output;inputs[0]=&input;outputs[0]=NULL;
    CHECK(api->process(state,&ctx)==-77 && external.valid);
    CHECK(!memcmp(data,external.data,sizeof(data)));
    inputs[0]=NULL;CHECK(api->process(state,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->reset(state)==0 && !external.valid && !external.ready);
    ctx.frame_count=0;calls=0;CHECK(api->process(state,&ctx)==0 && calls==0);
    values[0].value.i32=64;config.channels=64;config.block_size=128;
    CHECK(api->prepare(state,&config)==0 && external.channels==64 && external.frames==128);
    config.block_size=129;CHECK(api->prepare(state,&config)!=0 && external.frames==128);
    CHECK(api->destroy(state)==0);
    return 0;
}
