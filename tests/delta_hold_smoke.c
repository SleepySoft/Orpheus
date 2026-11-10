#include "component_state.h"
#include <math.h>
#include <stdio.h>
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
static int maximum_shape(const OrpheusComponentInterface* api) {
    float input[64*64];int32_t output[64*64];
    const char* ids[]={"channels","batches","hold_ticks"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=64},
        {.type=ORPHEUS_VALUE_INT,.value.i32=64},{.type=ORPHEUS_VALUE_INT,.value.i32=2}};
    orpheus_builtin_delta_hold_State state;void* handle=NULL;
    OrpheusConfig config={.sample_rate=48000,.block_size=8,.state_block=&state,
        .param_ids=ids,.param_values=params,.param_count=3};
    OrpheusBuffer in={.data=input,.channels=64,.frame_count=64,.frame_capacity=64,
        .format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer out={.data=output,.channels=64,.frame_capacity=64,
        .format=ORPHEUS_FORMAT_I32,.interleaved=true};
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&out};
    OrpheusContinuation continuation={.emit=emit};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,
        .frame_count=8,.continuation=&continuation};
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0);
    for(unsigned h=0;h<64;h++) for(unsigned c=0;c<64;c++)
        input[h*64+c]=(h+c)%8==0 ? 4.0f : 0.0f;
    calls=0;CHECK(api->process(handle,&ctx)==0 && calls==1 && out.frame_count==64);
    for(unsigned c=0;c<64;c++) {
        int32_t active=0;unsigned counter=0;float previous=0;
        for(unsigned h=0;h<64;h++) {
            float current=input[h*64+c];
            if(current-previous>1.5f) {active=1;counter=0;}
            else if(active && counter<2) counter++;
            else active=0;
            CHECK(output[h*64+c]==active);previous=current;
        }
        CHECK(state.algorithm.active[c]==active && state.algorithm.counter[c]==counter
            && state.algorithm.previous[c]==previous);
    }
    params[0].value.i32=65;CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG);
    params[0].value.i32=1;params[1].value.i32=2;params[2].value.i32=INT32_MAX;
    CHECK(api->prepare(handle,&config)==0);
    in.channels=out.channels=1;in.frame_count=2;input[0]=input[1]=0;
    state.algorithm.active[0]=1;state.algorithm.counter[0]=INT32_MAX-1;
    CHECK(api->process(handle,&ctx)==0 && output[0]==1 && output[1]==0
        && state.algorithm.counter[0]==INT32_MAX && state.algorithm.active[0]==0);
    CHECK(api->destroy(handle)==0);
    return 0;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    CHECK(maximum_shape(api)==0);
    float input[6],previous[2]={0};int32_t output[6],expected[6],active[2]={0};unsigned counters[2]={0};
    const char* ids[]={"channels","batches","hold_ticks"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=2},{.type=ORPHEUS_VALUE_INT,.value.i32=3},
                          {.type=ORPHEUS_VALUE_INT,.value.i32=2}};
    OrpheusConfig config={.sample_rate=48000,.block_size=1,.param_ids=ids,.param_values=params,.param_count=3};
    OrpheusBuffer in={.data=input,.channels=2,.frame_count=3,.frame_capacity=3,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer out={.data=output,.channels=2,.frame_capacity=3,.format=ORPHEUS_FORMAT_I32,.interleaved=true};
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&out};
    OrpheusContinuation continuation={.emit=emit};
    OrpheusProcessContext context={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,.frame_count=8,
        .continuation=&continuation};
    orpheus_builtin_delta_hold_State state;void* allocated=NULL;config.state_block=&state;
    CHECK(api->create(&allocated,&config)==0 && api->prepare(&state,&config)==ORPHEUS_OK);
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
        orpheus_builtin_delta_hold_State saved=state;out.frame_capacity=2;
        CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&saved,&state,sizeof(state)));out.frame_capacity=3;
        out.format=ORPHEUS_FORMAT_F32;CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG);out.format=ORPHEUS_FORMAT_I32;
        out.data=input;CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG);out.data=output;
        context.frame_count=0;calls=0;
        CHECK(api->process(&state,&context)==ORPHEUS_OK && !calls && !memcmp(&saved,&state,sizeof(state)));context.frame_count=8;
        for(unsigned h=0;h<3;++h) for(unsigned c=0;c<2;++c) {
            unsigned i=h*2+c;float current=input[i];volatile float delta=current-previous[c];int32_t flag;
            if(delta>1.5f) {flag=1;counters[c]=0;}
            else if(counters[c]<hold && active[c]==1) {flag=1;++counters[c];}
            else flag=0;
            expected[i]=flag;active[c]=flag;previous[c]=current;
        }
        outputs[0]=call==2 ? NULL : &out;
        CHECK(api->process(&state,&context)==ORPHEUS_OK && calls==1);
        if(outputs[0]) CHECK(out.frame_count==3 && !memcmp(output,expected,sizeof(output)));
        CHECK(!memcmp(state.algorithm.previous,previous,sizeof(previous)) && !memcmp(state.algorithm.active,active,sizeof(active)) &&
              !memcmp(state.algorithm.counter,counters,sizeof(counters)));
        outputs[0]=&out;
    }
    OrpheusValue value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=NAN};
    CHECK(api->set_parameter(&state,"threshold",&value)==ORPHEUS_ERR_INVALID_ARG);
    value.type=ORPHEUS_VALUE_INT;value.value.i32=-1;CHECK(api->set_parameter(&state,"hold_ticks",&value)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->reset(&state)==0);
    value.type=ORPHEUS_VALUE_INT;value.value.i32=3;CHECK(api->set_parameter(&state,"hold_ticks",&value)==0);
    for(unsigned i=0;i<6;i++) input[i]=4.0f;
    calls=0;fail=-77;CHECK(api->process(&state,&context)==-77 && calls==1);fail=0;
    for(unsigned c=0;c<2;c++) CHECK(state.algorithm.previous[c]==4.0f
        && state.algorithm.active[c]==1 && state.algorithm.counter[c]==2);
    CHECK(api->destroy(allocated)==ORPHEUS_OK);
    CHECK(api->create(&allocated,&config)==ORPHEUS_OK && allocated==&state);
    CHECK(api->prepare(allocated,&config)==ORPHEUS_OK && api->destroy(allocated)==ORPHEUS_OK);
    config.state_block=NULL;
    CHECK(api->create(&allocated,&config)==ORPHEUS_ERR_INVALID_ARG);
    return 0;
}
