#include "component_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); return 1; } } while (0)
static unsigned calls;
static int fail;
static int emit(void* user,uint32_t port,const OrpheusDelivery* delivery) {
    (void)user;
    if(port!=1 || !delivery) return ORPHEUS_ERR_INVALID_ARG;
    calls++;
    return fail;
}
static int maximum_shapes(const OrpheusComponentInterface* api) {
    const char* ids[]={"channels","bins","batches","capacity_elements","delay_elements"};
    for(unsigned shape=0;shape<2;shape++) {
        unsigned channels=shape ? 1 : 64,bins=65536,batches=shape ? 64 : 1;
        unsigned capacity=shape ? 1048576 : 65536;
        size_t count=(size_t)channels*bins*batches,pool=(size_t)channels*capacity;
        uint32_t* input=malloc(count*4);uint32_t* output=malloc(count*4);CHECK(input && output);
        for(size_t i=0;i<count;i++) input[i]=0x3f000000u|((uint32_t)i*2654435761u&0x007fffffu);
        OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=channels},
            {.type=ORPHEUS_VALUE_INT,.value.i32=bins},{.type=ORPHEUS_VALUE_INT,.value.i32=batches},
            {.type=ORPHEUS_VALUE_INT,.value.i32=capacity},{.type=ORPHEUS_VALUE_INT,.value.i32=0}};
        orpheus_builtin_bulk_ring_delay_State state;void* handle=NULL;
        OrpheusConfig config={.sample_rate=48000,.block_size=8,.state_block=&state,
            .param_ids=ids,.param_values=params,.param_count=5};
        OrpheusBuffer in={.data=input,.channels=channels,.frame_count=bins*batches,.frame_capacity=bins*batches,
            .format=ORPHEUS_FORMAT_F32,.interleaved=true};
        OrpheusBuffer out=in;out.data=output;out.frame_count=0;
        const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&out};
        OrpheusContinuation continuation={.emit=emit};
        OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,
            .frame_count=8,.continuation=&continuation};
        CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0);
        calls=0;
        CHECK(api->process(handle,&ctx)==0 && calls==1 && out.frame_count==bins*batches);
        CHECK(!memcmp(input,output,count*4) && state.algorithm.cursor==0);
        CHECK(state.algorithm.count==pool && !memcmp(state.algorithm.history,input+count-pool,pool*4));
        params[1].value.i32=65537;
        CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG);
        params[1].value.i32=bins;params[3].value.i32=1048577;
        CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && state.algorithm.count==pool);
        CHECK(api->destroy(handle)==0);
        free(input);free(output);
    }
    return 0;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    CHECK(maximum_shapes(api)==0);
    const uint32_t bits[]={0,0x80000000u,0x7f800000u,0xff800000u,0x7fc12345u,0x7f812345u,0x3f99999au,1};
    uint32_t input[28],output[28],expected[28],history[22]={0};unsigned cursor=0;
    const char* ids[]={"channels","bins","batches","capacity_elements","delay_elements"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=2},{.type=ORPHEUS_VALUE_INT,.value.i32=7},
                          {.type=ORPHEUS_VALUE_INT,.value.i32=2},{.type=ORPHEUS_VALUE_INT,.value.i32=11},
                          {.type=ORPHEUS_VALUE_INT,.value.i32=0}};
    OrpheusConfig config={.sample_rate=48000,.block_size=1,.param_ids=ids,.param_values=params,.param_count=5};
    OrpheusBuffer in={.data=input,.channels=2,.frame_count=14,.frame_capacity=14,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer out={.data=output,.channels=2,.frame_capacity=14,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&out};
    OrpheusContinuation continuation={.emit=emit};
    OrpheusProcessContext context={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,.frame_count=8,
        .continuation=&continuation};
    orpheus_builtin_bulk_ring_delay_State state;void* allocated=NULL;config.state_block=&state;
    CHECK(api->create(&allocated,&config)==0 && api->prepare(&state,&config)==ORPHEUS_OK);
    const int delays[]={0,1,7,10,11,34,INT32_MAX};
    for(unsigned call=0;call<21;++call) {
        int delay=delays[call%7];
        for(unsigned i=0;i<28;++i) input[i]=bits[(i+call)%8];
        OrpheusValue value={.type=ORPHEUS_VALUE_INT,.value.i32=delay};
        CHECK(api->set_parameter(&state,"delay_elements",&value)==ORPHEUS_OK);
        if(call==12) {CHECK(api->reset(&state)==ORPHEUS_OK);memset(history,0,sizeof(history));cursor=0;}
        uint32_t saved[22];memcpy(saved,state.algorithm.history,sizeof(saved));unsigned saved_cursor=state.algorithm.cursor;
        out.frame_capacity=13;
        CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG && !memcmp(saved,state.algorithm.history,sizeof(saved)) && state.algorithm.cursor==saved_cursor);
        out.frame_capacity=14;out.data=input;CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG);out.data=output;
        context.frame_count=0;calls=0;
        CHECK(api->process(&state,&context)==ORPHEUS_OK && !calls && !memcmp(saved,state.algorithm.history,sizeof(saved))
            && state.algorithm.cursor==saved_cursor);context.frame_count=8;
        for(unsigned h=0;h<2;++h) {
            for(unsigned k=0;k<7;++k) for(unsigned c=0;c<2;++c) history[((cursor+k)%11)*2+c]=input[(h*7+k)*2+c];
            unsigned read=(cursor+11-(unsigned)delay%11)%11;
            for(unsigned k=0;k<7;++k) for(unsigned c=0;c<2;++c) expected[(h*7+k)*2+c]=history[((read+k)%11)*2+c];
            cursor=(cursor+7)%11;
        }
        outputs[0]=call==2 ? NULL : &out;
        CHECK(api->process(&state,&context)==ORPHEUS_OK && calls==1 && state.algorithm.cursor==cursor
            && !memcmp(state.algorithm.history,history,sizeof(history)));
        if(outputs[0]) CHECK(out.frame_count==14 && !memcmp(output,expected,sizeof(output)));
        outputs[0]=&out;
    }
    params[3].value.i32=6;CHECK(api->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG);params[3].value.i32=11;
    OrpheusValue bad={.type=ORPHEUS_VALUE_INT,.value.i32=-1};CHECK(api->set_parameter(&state,"delay_elements",&bad)==ORPHEUS_ERR_INVALID_ARG);
    unsigned before=state.algorithm.cursor;calls=0;fail=-77;
    CHECK(api->process(&state,&context)==-77 && calls==1 && state.algorithm.cursor==(before+14)%11);fail=0;
    CHECK(api->destroy(&state)==ORPHEUS_OK && state.algorithm.history==NULL);
    CHECK(api->create(&allocated,&config)==ORPHEUS_OK && allocated==&state);
    CHECK(api->prepare(allocated,&config)==ORPHEUS_OK && api->destroy(allocated)==ORPHEUS_OK);
    config.state_block=NULL;
    CHECK(api->create(&allocated,&config)==ORPHEUS_ERR_INVALID_ARG);
    return 0;
}
