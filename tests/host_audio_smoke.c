#include "component_state.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>
#define CHECK(x) do {if (!(x)) {fprintf(stderr,"%d: %s\n",__LINE__,#x);return 1;}} while(0)
extern const OrpheusComponentInterface* orpheus_get_interface(void);
typedef orpheus_builtin_host_audio_State HostAudioState;
static int calls;
static int slots_seen;
static int deliver(void* user,uint32_t port,const OrpheusDelivery* delivery) {
    (void)user;++calls;
    return port==1 && delivery && delivery->frame_index==7 && delivery->epoch==3 &&
        (!delivery->buffer || delivery->buffer->frame_count==4) ? -77 : -99;
}
static OrpheusSlotId check_slot(void* user,const OrpheusSlotInfo* slot) {
    (void)user;
    if(!strcmp(slot->key,"samples") && slot->count==12 && slot->type==ORPHEUS_VALUE_FLOAT &&
       (slot->flags&ORPHEUS_SLOT_DOUBLE_BUFFERED)) ++slots_seen;
    if(!strcmp(slot->key,"ready") && slot->kind==ORPHEUS_SLOT_SETTING &&
       slot->type==ORPHEUS_VALUE_BOOL && slot->count==1) ++slots_seen;
    return 0;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    HostAudioState external;void* state=NULL;
    const char* ids[]={"channels","mode","ready"};
    OrpheusValue values[]={{.type=ORPHEUS_VALUE_INT,.value.i32=3},{.type=ORPHEUS_VALUE_INT,.value.i32=0},
        {.type=ORPHEUS_VALUE_BOOL,.value.b=true}};
    OrpheusConfig config={.block_size=4,.channels=3,.state_block=&external,
        .param_count=2,.param_ids=ids,.param_values=values};
    CHECK(api->create(&state,&config)==0 && state==&external && api->prepare(state,&config)==0);
    float data[12]={0};
    OrpheusBuffer output={.data=data,.channels=3,.frame_capacity=4,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    const OrpheusBuffer* inputs[]={NULL,NULL};OrpheusBuffer* outputs[]={&output};
    OrpheusContinuation continuation={.emit=deliver};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=2,.output_count=1,
        .frame_count=4,.frame_index=7,.epoch=3,.continuation=&continuation};
    OrpheusRegistry registry={.add=check_slot};
    CHECK(api->register_slots(state,&registry)==0 && slots_seen==2);
    CHECK(api->process(state,&ctx)==ORPHEUS_ERR_PROCESSING && calls==0 && external.missing_blocks==1);
    external.missing_blocks=INT_MAX;
    CHECK(api->process(state,&ctx)==ORPHEUS_ERR_PROCESSING && external.missing_blocks==INT_MAX);
    for (unsigned i=0;i<12;++i) external.algorithm.data[i]=(float)i-3.5f;
    OrpheusDelivery tick={.frame_index=7,.epoch=3};
    ctx.trigger_input=1;ctx.delivery=&tick;
    OrpheusValue ready={.type=ORPHEUS_VALUE_BOOL,.value.b=true};
    CHECK(api->set_parameter(state,"ready",&ready)==0);
    output.frame_capacity=3;CHECK(api->process(state,&ctx)!=0 && external.ready);output.frame_capacity=4;
    output.channels=2;CHECK(api->process(state,&ctx)!=0 && external.ready);output.channels=3;
    output.data=external.algorithm.data+1;CHECK(api->process(state,&ctx)!=0 && external.ready);output.data=data;
    output.data=&external.mode;CHECK(api->process(state,&ctx)!=0 && external.ready);output.data=data;
    output.format=ORPHEUS_FORMAT_I32;CHECK(api->process(state,&ctx)!=0 && external.ready);output.format=ORPHEUS_FORMAT_F32;
    CHECK(api->process(state,&ctx)==-77 && calls==1 && !external.ready && output.frame_count==4);
    for (unsigned i=0;i<12;++i) CHECK(data[i]==(float)i-3.5f);
    outputs[0]=NULL;CHECK(api->set_parameter(state,"ready",&ready)==0);
    CHECK(api->process(state,&ctx)==-77 && !external.ready);outputs[0]=&output;
    float upstream[12];
    for (unsigned i=0;i<12;++i) upstream[i]=(float)i+7.25f;
    OrpheusBuffer source=output;source.data=upstream;
    inputs[0]=&source;
    ctx.trigger_input=0;ctx.delivery=NULL;
    CHECK(api->set_parameter(state,"ready",&ready)==0);
    int before=calls;
    CHECK(api->process(state,&ctx)==ORPHEUS_ERR_INVALID_ARG && calls==before && external.ready);
    ready.value.b=false;CHECK(api->set_parameter(state,"ready",&ready)==0);ready.value.b=true;
    CHECK(api->process(state,&ctx)==-77 && calls==before+1 && !external.ready);
    CHECK(!memcmp(upstream,data,sizeof(data)) && !memcmp(upstream,external.algorithm.data,sizeof(data)));
    source.frame_count=3;before=calls;
    CHECK(api->process(state,&ctx)==ORPHEUS_ERR_INVALID_ARG && calls==before);source.frame_count=4;
    source.data=data;before=calls;
    CHECK(api->process(state,&ctx)==ORPHEUS_ERR_INVALID_ARG && calls==before);source.data=upstream;
    inputs[0]=NULL;
    HostAudioState snapshot=external;
    values[0].value.i32=65;CHECK(api->prepare(state,&config)!=0 && !memcmp(&external,&snapshot,sizeof(external)));
    values[0].value.i32=3;config.param_count=3;
    CHECK(api->prepare(state,&config)!=0 && !memcmp(&external,&snapshot,sizeof(external)));
    config.param_count=2;
    values[0].value.i32=3;values[1].value.i32=1;
    CHECK(api->prepare(state,&config)==0 && api->set_parameter(state,"ready",&ready)!=0);
    OrpheusBuffer input=output;inputs[0]=&input;outputs[0]=NULL;
    CHECK(api->process(state,&ctx)==-77 && external.valid);
    CHECK(!memcmp(data,external.algorithm.data,sizeof(data)));
    OrpheusValue probe={.type=ORPHEUS_VALUE_INT,.value.i32=0};
    CHECK(api->set_parameter(state,"missing_blocks",&probe)==ORPHEUS_ERR_UNSUPPORTED);
    probe.type=ORPHEUS_VALUE_BOOL;probe.value.b=false;
    CHECK(api->set_parameter(state,"valid",&probe)==ORPHEUS_ERR_UNSUPPORTED && external.valid);
    inputs[0]=NULL;CHECK(api->process(state,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->reset(state)==0 && !external.valid && !external.ready && !external.missing_blocks);
    for(unsigned i=0;i<8192;++i) CHECK(external.algorithm.data[i]==0);
    ctx.frame_count=0;calls=0;CHECK(api->process(state,&ctx)==0 && calls==0);
    values[0].value.i32=64;config.channels=64;config.block_size=128;
    CHECK(api->prepare(state,&config)==0 && external.channels==64 && external._orpheus_config_frames==128);
    config.block_size=129;CHECK(api->prepare(state,&config)!=0 && external._orpheus_config_frames==128);
    config.block_size=0;CHECK(api->prepare(state,&config)!=0 && external._orpheus_config_frames==128);
    CHECK(api->destroy(state)==0);
    return 0;
}
