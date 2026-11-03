#include "orpheus_delta_hold.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); return 1; } } while (0)
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    float input[6],previous[2]={0};int32_t output[6],expected[6],active[2]={0};unsigned counters[2]={0};
    const char* ids[]={"channels","batches","hold_ticks"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=2},{.type=ORPHEUS_VALUE_INT,.value.i32=3},
                          {.type=ORPHEUS_VALUE_INT,.value.i32=2}};
    OrpheusConfig config={.sample_rate=48000,.block_size=1,.param_ids=ids,.param_values=params,.param_count=3};
    OrpheusBuffer in={.data=input,.channels=2,.frame_count=3,.frame_capacity=3,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer out={.data=output,.channels=2,.frame_capacity=3,.format=ORPHEUS_FORMAT_I32,.interleaved=true};
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&out};
    OrpheusProcessContext context={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,.frame_count=1};
    DeltaHoldState state={0};CHECK(api->prepare(&state,&config)==ORPHEUS_OK);
    const float sequence[]={1.5f,3.0f,5.0f,5.0f,5.0f,5.0f,NAN,INFINITY,INFINITY,-INFINITY,0,-0.0f};
    for(unsigned call=0;call<12;++call) {
        unsigned hold=call>=6 ? 0 : 2;
        OrpheusValue value={.type=ORPHEUS_VALUE_INT,.value.i32=hold};
        CHECK(api->set_parameter(&state,"hold_ticks",&value)==ORPHEUS_OK);
        if(call==5) {
            CHECK(api->reset(&state)==ORPHEUS_OK);
            memset(previous,0,sizeof(previous));memset(active,0,sizeof(active));memset(counters,0,sizeof(counters));
        }
        for(unsigned i=0;i<6;++i) input[i]=sequence[(call*3+i/2+(i%2)*2)%12];
        DeltaHoldState saved=state;out.frame_capacity=2;
        CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&saved,&state,sizeof(state)));out.frame_capacity=3;
        out.format=ORPHEUS_FORMAT_F32;CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG);out.format=ORPHEUS_FORMAT_I32;
        out.data=input;CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG);out.data=output;
        context.frame_count=0;CHECK(api->process(&state,&context)==ORPHEUS_OK && !memcmp(&saved,&state,sizeof(state)));context.frame_count=1;
        for(unsigned h=0;h<3;++h) for(unsigned c=0;c<2;++c) {
            unsigned i=h*2+c;float current=input[i];volatile float delta=current-previous[c];int32_t flag;
            if(delta>1.5f) {flag=1;counters[c]=0;}
            else if(counters[c]<hold && active[c]==1) {flag=1;++counters[c];}
            else flag=0;
            expected[i]=flag;active[c]=flag;previous[c]=current;
        }
        outputs[0]=call==2 ? NULL : &out;
        CHECK(api->process(&state,&context)==ORPHEUS_OK);
        if(outputs[0]) CHECK(out.frame_count==3 && !memcmp(output,expected,sizeof(output)));
        CHECK(!memcmp(state.previous,previous,sizeof(previous)) && !memcmp(state.active,active,sizeof(active)) &&
              !memcmp(state.counter,counters,sizeof(counters)));
        outputs[0]=&out;
    }
    OrpheusValue value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=NAN};
    CHECK(api->set_parameter(&state,"threshold",&value)==ORPHEUS_ERR_INVALID_ARG);
    value.type=ORPHEUS_VALUE_INT;value.value.i32=-1;CHECK(api->set_parameter(&state,"hold_ticks",&value)==ORPHEUS_ERR_INVALID_ARG);
    config.state_block=&state;void* allocated=NULL;
    CHECK(api->create(&allocated,&config)==ORPHEUS_OK && allocated==&state && !state.owns_memory);
    CHECK(api->prepare(allocated,&config)==ORPHEUS_OK && api->destroy(allocated)==ORPHEUS_OK);
    config.state_block=NULL;
    CHECK(api->create(&allocated,&config)==ORPHEUS_OK && ((DeltaHoldState*)allocated)->owns_memory);
    CHECK(api->destroy(allocated)==ORPHEUS_OK);
    return 0;
}
