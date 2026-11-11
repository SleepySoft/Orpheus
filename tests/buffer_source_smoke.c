#include "component_state.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); return 1; } } while (0)
extern const OrpheusComponentInterface* orpheus_get_interface(void);
typedef orpheus_builtin_buffer_source_State BufferSourceState;
static int calls;
static int slots_seen;
static int deliver(void* user, uint32_t port,const OrpheusDelivery* delivery) {
    (void)user;
    ++calls;
    return port == 1 && delivery && delivery->frame_index==11 && delivery->epoch==3 &&
        (!delivery->buffer || delivery->buffer->frame_count==4) ? -77 : -99;
}
static OrpheusSlotId check_slot(void* user,const OrpheusSlotInfo* slot) {
    (void)user;
    if(!strcmp(slot->key,"samples") && slot->count==12 &&
       slot->type==ORPHEUS_VALUE_FLOAT && (slot->flags&ORPHEUS_SLOT_DOUBLE_BUFFERED)) ++slots_seen;
    return 0;
}

int main(void) {
    const OrpheusComponentInterface* api = orpheus_get_interface();
    const OrpheusComponentDescriptor* descriptor = api->get_descriptor();
    CHECK(!strcmp(descriptor->id, "orpheus.builtin.buffer_source"));
    CHECK(descriptor->port_count == 1 && descriptor->ports[0].direction == ORPHEUS_PORT_OUTPUT);
    BufferSourceState external;
    void* state = NULL;
    const char* ids[] = {"channels","ready"};
    OrpheusValue channels = {.type=ORPHEUS_VALUE_INT, .value.i32=3};
    OrpheusConfig config = {.block_size=4, .channels=3, .state_block=&external,
        .param_count=1, .param_ids=ids, .param_values=&channels};
    CHECK(api->create(&state, &config) == 0 && api->prepare(state, &config) == 0);
    float data[12] = {0};
    OrpheusBuffer output = {.data=data, .channels=3, .frame_capacity=4,
        .format=ORPHEUS_FORMAT_F32, .interleaved=true};
    OrpheusBuffer* outputs[] = {&output};
    OrpheusContinuation continuation = {.emit=deliver};
    OrpheusProcessContext ctx = {.outputs=outputs, .output_count=1,
        .frame_count=4, .frame_index=11,.epoch=3,.trigger_input=UINT32_MAX,.continuation=&continuation};
    OrpheusRegistry registry={.add=check_slot};
    CHECK(api->register_slots(state,&registry)==0 && slots_seen==1);
    CHECK(api->process(state, &ctx) == ORPHEUS_ERR_PROCESSING && calls == 0 && external.missing_blocks==1);
    external.missing_blocks=INT_MAX;
    CHECK(api->process(state,&ctx)==ORPHEUS_ERR_PROCESSING && external.missing_blocks==INT_MAX && calls==0);
    for (unsigned i=0; i<12; ++i) external.algorithm.data[i] = (float)i - 3.5f;
    OrpheusValue ready = {.type=ORPHEUS_VALUE_BOOL, .value.b=true};
    CHECK(api->set_parameter(state, "ready", &ready) == 0);
    output.frame_capacity = 3;
    CHECK(api->process(state, &ctx) == ORPHEUS_ERR_INVALID_ARG && external.ready);
    output.frame_capacity = 4;
    output.channels=2;CHECK(api->process(state,&ctx)==ORPHEUS_ERR_INVALID_ARG && external.ready);output.channels=3;
    output.data=external.algorithm.data+1;
    CHECK(api->process(state,&ctx)==ORPHEUS_ERR_INVALID_ARG && external.ready);output.data=data;
    ctx.frame_count=3;CHECK(api->process(state,&ctx)==ORPHEUS_ERR_INVALID_ARG && external.ready);ctx.frame_count=4;
    CHECK(api->process(state, &ctx) == -77 && calls == 1 && !external.ready);
    CHECK(output.frame_count == 4 && !memcmp(data, external.algorithm.data, sizeof(data)));
    CHECK(api->process(state, &ctx) == ORPHEUS_ERR_PROCESSING && calls == 1);
    CHECK(api->set_parameter(state,"ready",&ready)==0);
    ctx.frame_count=0;CHECK(api->process(state,&ctx)==0 && external.ready && calls==1);ctx.frame_count=4;
    outputs[0]=NULL;
    CHECK(api->process(state,&ctx)==-77 && !external.ready && calls==2);outputs[0]=&output;
    BufferSourceState snapshot=external;
    OrpheusValue invalid_values[]={channels,ready};
    config.param_values=invalid_values;config.param_count=2;
    CHECK(api->prepare(state,&config)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&external,&snapshot,sizeof(external)));
    config.param_values=&channels;config.param_count=1;
    CHECK(api->set_parameter(state,"missing_blocks",&channels)==ORPHEUS_ERR_UNSUPPORTED);
    channels.value.i32 = 64;
    config.block_size = 129;
    CHECK(api->prepare(state, &config) == ORPHEUS_ERR_INVALID_ARG && !memcmp(&external,&snapshot,sizeof(external)));
    config.block_size=128;
    CHECK(api->prepare(state,&config)==0 && external._orpheus_config_frames==128 && external.channels==64);
    config.block_size=0;
    CHECK(api->prepare(state,&config)==ORPHEUS_ERR_INVALID_ARG && external._orpheus_config_frames==128);
    CHECK(api->reset(state) == 0 && !external.ready && external.missing_blocks == 0);
    for(unsigned i=0;i<8192;++i) CHECK(external.algorithm.data[i]==0);
    CHECK(api->destroy(state) == 0);
    return 0;
}
