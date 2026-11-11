#include "component_state.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); return 1; } } while (0)
typedef struct Capture {
    unsigned calls;
    unsigned count;
    int error;
    OrpheusSlotInfo slot;
} Capture;
static int emit(void* user,uint32_t port,const OrpheusDelivery* delivery) {
    Capture* capture=user;
    if(port!=1 || !delivery || delivery->buffer || delivery->frame_index!=123 || delivery->epoch!=7)
        return ORPHEUS_ERR_INVALID_ARG;
    capture->calls++;
    return capture->error;
}
static OrpheusSlotId add(void* user,const OrpheusSlotInfo* slot) {
    Capture* capture=user;
    if(!strcmp(slot->key,"values_storage")) capture->slot=*slot;
    return (OrpheusSlotId)++capture->count;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    CHECK(api->get_descriptor()->abi_version==7);
    const char* ids[]={"count","initial_values"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=4},
        {.type=ORPHEUS_VALUE_STRING,.value.str="-0,1.25 -2.5"}};
    orpheus_builtin_bulk_relay_State state;
    OrpheusConfig config={.sample_rate=48000,.block_size=8,.state_block=&state,
        .param_count=2,.param_ids=ids,.param_values=params};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0);
    CHECK(sizeof(state.values)/sizeof(float)==2048 && state.count==4);
    float expected[]={-0.0f,1.25f,-2.5f,0.0f};
    CHECK(!memcmp(state.values,expected,sizeof(expected)));
    for(unsigned i=4;i<2048;i++) CHECK(state.values[i]==0);
    Capture capture={0};
    OrpheusRegistry registry={.ctx=&capture,.add=add};
    CHECK(api->register_slots(handle,&registry)==0);
    CHECK(capture.slot.kind==ORPHEUS_SLOT_BULK && capture.slot.count==4
        && capture.slot.type==ORPHEUS_VALUE_FLOAT && capture.slot.offset==offsetof(orpheus_builtin_bulk_relay_State,values)
        && (capture.slot.flags & ORPHEUS_SLOT_DOUBLE_BUFFERED));
    OrpheusContinuation continuation={.emit=emit,.user=&capture};
    OrpheusProcessContext ctx={.frame_count=8,.valid_frames=8,.frame_index=123,.epoch=7,
        .trigger_input=0,.continuation=&continuation};
    CHECK(api->process(handle,&ctx)==0 && capture.calls==1);
    capture.error=-77;
    CHECK(api->process(handle,&ctx)==-77 && capture.calls==2);
    CHECK(!memcmp(state.values,expected,sizeof(expected)));
    CHECK(api->reset(handle)==0 && !memcmp(state.values,expected,sizeof(expected)));
    ctx.frame_count=0;capture.error=0;
    CHECK(api->process(handle,&ctx)==0 && capture.calls==3);
    const char* invalid[]={"x","1x","nan","inf","1e999","1,2,3,4,5"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++) {
        params[1].value.str=invalid[i];
        CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG);
        CHECK(state.count==4 && !memcmp(state.values,expected,sizeof(expected)));
    }
    params[1].value.str="";params[0].value.i32=2048;
    CHECK(api->prepare(handle,&config)==0 && state.count==2048);
    for(unsigned i=0;i<2048;i++) CHECK(state.values[i]==0);
    capture.count=0;CHECK(api->register_slots(handle,&registry)==0 && capture.slot.count==2048);
    params[0].value.i32=0;
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && state.count==2048);
    params[0].value.i32=2049;
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && state.count==2048);
    CHECK(api->destroy(handle)==0);
    return 0;
}
