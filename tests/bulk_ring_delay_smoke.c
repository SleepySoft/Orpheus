#include "orpheus_bulk_ring_delay.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); return 1; } } while (0)
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
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
    OrpheusProcessContext context={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,.frame_count=1};
    BulkRingDelayState state={0};CHECK(api->prepare(&state,&config)==ORPHEUS_OK);
    const int delays[]={0,1,7,10,11,34,INT32_MAX};
    for(unsigned call=0;call<21;++call) {
        int delay=delays[call%7];
        for(unsigned i=0;i<28;++i) input[i]=bits[(i+call)%8];
        OrpheusValue value={.type=ORPHEUS_VALUE_INT,.value.i32=delay};
        CHECK(api->set_parameter(&state,"delay_elements",&value)==ORPHEUS_OK);
        if(call==12) {CHECK(api->reset(&state)==ORPHEUS_OK);memset(history,0,sizeof(history));cursor=0;}
        uint32_t saved[22];memcpy(saved,state.history,sizeof(saved));unsigned saved_cursor=state.cursor;
        out.frame_capacity=13;
        CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG && !memcmp(saved,state.history,sizeof(saved)) && state.cursor==saved_cursor);
        out.frame_capacity=14;out.data=input;CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG);out.data=output;
        context.frame_count=0;CHECK(api->process(&state,&context)==ORPHEUS_OK && !memcmp(saved,state.history,sizeof(saved)) && state.cursor==saved_cursor);context.frame_count=1;
        for(unsigned h=0;h<2;++h) {
            for(unsigned k=0;k<7;++k) for(unsigned c=0;c<2;++c) history[((cursor+k)%11)*2+c]=input[(h*7+k)*2+c];
            unsigned read=(cursor+11-(unsigned)delay%11)%11;
            for(unsigned k=0;k<7;++k) for(unsigned c=0;c<2;++c) expected[(h*7+k)*2+c]=history[((read+k)%11)*2+c];
            cursor=(cursor+7)%11;
        }
        outputs[0]=call==2 ? NULL : &out;
        CHECK(api->process(&state,&context)==ORPHEUS_OK && state.cursor==cursor && !memcmp(state.history,history,sizeof(history)));
        if(outputs[0]) CHECK(out.frame_count==14 && !memcmp(output,expected,sizeof(output)));
        outputs[0]=&out;
    }
    params[3].value.i32=6;CHECK(api->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG);params[3].value.i32=11;
    OrpheusValue bad={.type=ORPHEUS_VALUE_INT,.value.i32=-1};CHECK(api->set_parameter(&state,"delay_elements",&bad)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->destroy(&state)==ORPHEUS_OK && state.history==NULL);
    config.state_block=&state;void* allocated=NULL;
    CHECK(api->create(&allocated,&config)==ORPHEUS_OK && allocated==&state && !state.owns_memory);
    CHECK(api->prepare(allocated,&config)==ORPHEUS_OK && api->destroy(allocated)==ORPHEUS_OK);
    config.state_block=NULL;
    CHECK(api->create(&allocated,&config)==ORPHEUS_OK && ((BulkRingDelayState*)allocated)->owns_memory);
    CHECK(api->prepare(allocated,&config)==ORPHEUS_OK && api->destroy(allocated)==ORPHEUS_OK);
    return 0;
}
