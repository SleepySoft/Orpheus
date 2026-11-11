#include "component_state.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); return 1; } } while (0)
typedef struct Capture {
    unsigned calls;
    int error;
    uint64_t frame;
} Capture;
static int emit(void* user,uint32_t port,const OrpheusDelivery* delivery) {
    Capture* capture=user;
    if(port!=1 || !delivery || delivery->buffer || delivery->frame_index!=capture->frame || delivery->epoch!=7)
        return ORPHEUS_ERR_INVALID_ARG;
    capture->calls++;
    return capture->error;
}
static int shape(const OrpheusComponentInterface* api,int factor,int phase) {
    const char* ids[]={"factor","phase"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=factor},
        {.type=ORPHEUS_VALUE_INT,.value.i32=phase}};
    orpheus_builtin_trigger_divider_State state;
    OrpheusConfig config={.sample_rate=48000,.block_size=8,.state_block=&state,
        .param_count=2,.param_ids=ids,.param_values=params};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0);
    Capture capture={0};
    OrpheusContinuation continuation={.emit=emit,.user=&capture};
    OrpheusProcessContext ctx={.epoch=7,.trigger_input=0,.continuation=&continuation};
    unsigned expected=0;
    for(unsigned i=0;i<2u*(unsigned)factor+1;i++) {
        ctx.frame_index=capture.frame=i/2*8;
        capture.error=-77;
        int rc=api->process(handle,&ctx);
        if(i%(unsigned)factor==(unsigned)phase) {
            expected++;
            CHECK(rc==-77);
        } else CHECK(rc==0);
        CHECK(capture.calls==expected && state.algorithm.counter==(i+1)%(unsigned)factor);
    }
    orpheus_builtin_trigger_divider_State saved=state;
    params[1].value.i32=factor;
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&saved,&state,sizeof(state)));
    CHECK(api->reset(handle)==0 && state.algorithm.counter==0 && state.factor==factor && state.phase==phase);
    CHECK(api->destroy(handle)==0);
    return 0;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    CHECK(shape(api,1,0)==0 && shape(api,2,0)==0 && shape(api,2,1)==0
        && shape(api,3,2)==0 && shape(api,192000,191999)==0);
    return 0;
}
