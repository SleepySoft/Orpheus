#include "component_state.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); return 1; } } while (0)
static unsigned calls, ports[2], fail_port;
static int emit(void* user,uint32_t port,const OrpheusDelivery* delivery) {
    (void)user;
    if(!delivery || port<1 || port>2) return ORPHEUS_ERR_INVALID_ARG;
    ports[calls++%2]=port;
    return port==fail_port ? -77 : ORPHEUS_OK;
}
static int maximum_shapes(const OrpheusComponentInterface* api) {
    const char* ids[]={"channels","bins","batches","initial"};
    for(unsigned shape=0;shape<2;shape++) {
        unsigned channels=shape ? 1 : 64,bins=65536,batches=shape ? 64 : 1;
        size_t points=(size_t)channels*bins,count=points*batches;
        float* input=malloc(count*sizeof(float));
        float* fast=malloc(count*sizeof(float));float* slow=malloc(count*sizeof(float));
        CHECK(input && fast && slow);
        for(size_t i=0;i<count;i++) input[i]=((int)(i%257)-128)*.03125f;
        OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=channels},
            {.type=ORPHEUS_VALUE_INT,.value.i32=bins},{.type=ORPHEUS_VALUE_INT,.value.i32=batches},
            {.type=ORPHEUS_VALUE_FLOAT,.value.f32=.25f}};
        orpheus_builtin_dual_trapezoid_smooth_State state;void* handle=NULL;
        OrpheusConfig config={.sample_rate=48000,.block_size=8,.state_block=&state,
            .param_ids=ids,.param_values=params,.param_count=4};
        OrpheusBuffer in={.data=input,.channels=channels,.frame_count=bins*batches,.frame_capacity=bins*batches,
            .format=ORPHEUS_FORMAT_F32,.interleaved=true};
        OrpheusBuffer a={.data=fast,.channels=channels,.frame_capacity=bins*batches,
            .format=ORPHEUS_FORMAT_F32,.interleaved=true};
        OrpheusBuffer b=a;b.data=slow;
        const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&a,&b};
        OrpheusContinuation continuation={.emit=emit};
        OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=2,
            .frame_count=8,.continuation=&continuation};
        CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0);
        calls=0;CHECK(api->process(handle,&ctx)==0 && calls==2 && ports[0]==1 && ports[1]==2);
        CHECK(a.frame_count==bins*batches && b.frame_count==bins*batches);
        const float alphas[]={.5f,.95f},factors[]={(1.0f-.5f)*.5f,(1.0f-.95f)*.5f};
        float* results[]={fast,slow};
        for(size_t i=0;i<points;i++) {
            float previous=.25f,history[]={.25f,.25f};
            for(unsigned h=0;h<batches;h++) {
                size_t index=h*points+i;float pair=input[index]+previous;
                for(unsigned p=0;p<2;p++) {
                    volatile float feed=pair*factors[p],feedback=history[p]*alphas[p];
                    float value=feed+feedback;history[p]=value;
                    CHECK(!memcmp(&value,results[p]+index,4));
                }
                previous=input[index];
            }
            CHECK(!memcmp(&previous,state.algorithm.history+i,4));
            for(unsigned p=0;p<2;p++) CHECK(!memcmp(history+p,state.algorithm.history+(p+1)*points+i,4));
        }
        params[1].value.i32=65537;
        CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && state.algorithm.count==points);
        CHECK(api->destroy(handle)==0);
        free(input);free(fast);free(slow);
    }
    return 0;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    CHECK(maximum_shapes(api)==0);
    const uint32_t bits[]={0,0x80000000u,0x7f800000u,0xff800000u,0x7fc12345u,0x7f812345u,0x3f99999au,1};
    float input[24],fast[24],slow[24],expected[2][24],hist[2][8]={{0}},previous[8]={0};
    const char* ids[]={"channels","bins","batches"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=2},{.type=ORPHEUS_VALUE_INT,.value.i32=4},
                          {.type=ORPHEUS_VALUE_INT,.value.i32=3}};
    OrpheusConfig config={.sample_rate=48000,.block_size=1,.param_ids=ids,.param_values=params,.param_count=3};
    OrpheusBuffer in={.data=input,.channels=2,.frame_count=12,.frame_capacity=12,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer a={.data=fast,.channels=2,.frame_capacity=12,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer b={.data=slow,.channels=2,.frame_capacity=12,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&a,&b};
    OrpheusContinuation continuation={.emit=emit};
    OrpheusProcessContext context={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=2,.frame_count=8,
        .continuation=&continuation};
    orpheus_builtin_dual_trapezoid_smooth_State state;void* allocated=NULL;config.state_block=&state;
    CHECK(api->create(&allocated,&config)==0 && api->prepare(&state,&config)==ORPHEUS_OK);
    for (unsigned call=0;call<5;++call) {
        float alphas[]={call>1 ? 0.0f : 0.5f,call>1 ? 1.0f : 0.95f};
        for (unsigned i=0;i<24;++i) {
            if(call==4) memcpy(input+i,&bits[i%8],4);
            else input[i]=((int)i-8)*(call+1)*0.03125f;
        }
        OrpheusValue value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=alphas[0]};
        CHECK(api->set_parameter(&state,"fast_alpha",&value)==ORPHEUS_OK);
        value.value.f32=alphas[1];CHECK(api->set_parameter(&state,"slow_alpha",&value)==ORPHEUS_OK);
        if(call==3) {
            CHECK(api->reset(&state)==ORPHEUS_OK);
            memset(hist,0,sizeof(hist));memset(previous,0,sizeof(previous));
        }
        float saved[24];memcpy(saved,state.algorithm.history,sizeof(saved));
        b.frame_capacity=11;
        CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG && !memcmp(saved,state.algorithm.history,sizeof(saved)));
        b.frame_capacity=12;b.data=fast;
        CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG && !memcmp(saved,state.algorithm.history,sizeof(saved)));b.data=slow;
        a.data=input;CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG);a.data=fast;
        context.frame_count=0;calls=0;
        CHECK(api->process(&state,&context)==ORPHEUS_OK && !calls && !memcmp(saved,state.algorithm.history,sizeof(saved)));context.frame_count=8;
        for(unsigned h=0;h<3;++h) for(unsigned i=0;i<8;++i) {
            unsigned index=h*8+i;float pair=input[index]+previous[i];
            for(unsigned p=0;p<2;++p) {
                volatile float feed=pair*((1.0f-alphas[p])*0.5f),feedback=hist[p][i]*alphas[p];
                expected[p][index]=feed+feedback;hist[p][i]=expected[p][index];
            }
            previous[i]=input[index];
        }
        outputs[0]=call==1 ? NULL : &a;outputs[1]=call==2 ? NULL : &b;
        CHECK(api->process(&state,&context)==ORPHEUS_OK && calls==2 && ports[0]==1 && ports[1]==2);
        if(outputs[0]) CHECK(a.frame_count==12 && !memcmp(fast,expected[0],sizeof(fast)));
        if(outputs[1]) CHECK(b.frame_count==12 && !memcmp(slow,expected[1],sizeof(slow)));
        CHECK(!memcmp(state.algorithm.history,previous,sizeof(previous)) &&
              !memcmp(state.algorithm.history+8,hist[0],sizeof(previous)) && !memcmp(state.algorithm.history+16,hist[1],sizeof(previous)));
        outputs[0]=&a;outputs[1]=&b;
    }
    OrpheusValue value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=NAN};
    CHECK(api->set_parameter(&state,"fast_alpha",&value)==ORPHEUS_ERR_INVALID_ARG);
    value.value.f32=-0.1f;CHECK(api->set_parameter(&state,"slow_alpha",&value)==ORPHEUS_ERR_INVALID_ARG);
    value.value.f32=INFINITY;CHECK(api->set_parameter(&state,"slow_alpha",&value)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->set_parameter(&state,"initial",&value)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->prepare(&state,&config)==ORPHEUS_OK && !state.algorithm.initialized);
    value.value.f32=4.0f;CHECK(api->set_parameter(&state,"initial",&value)==ORPHEUS_OK);
    value.value.f32=0.0f;
    CHECK(api->set_parameter(&state,"fast_alpha",&value)==ORPHEUS_OK &&
          api->set_parameter(&state,"slow_alpha",&value)==ORPHEUS_OK);
    for (unsigned i=0;i<24;++i) input[i]=2;
    b.frame_capacity=11;
    CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG && !state.algorithm.initialized);
    b.frame_capacity=12;context.frame_count=0;
    CHECK(api->process(&state,&context)==ORPHEUS_OK && !state.algorithm.initialized);context.frame_count=8;
    value.value.f32=6.0f;CHECK(api->set_parameter(&state,"initial",&value)==ORPHEUS_OK);
    CHECK(api->process(&state,&context)==ORPHEUS_OK && state.algorithm.initialized);
    CHECK(fast[0]==4 && slow[0]==4 && fast[8]==2 && fast[16]==2);
    value.value.f32=10.0f;CHECK(api->set_parameter(&state,"initial",&value)==ORPHEUS_OK);
    CHECK(api->process(&state,&context)==ORPHEUS_OK && fast[0]==2);
    CHECK(api->reset(&state)==ORPHEUS_OK && !state.algorithm.initialized);
    outputs[0]=outputs[1]=NULL;
    CHECK(api->process(&state,&context)==ORPHEUS_OK && state.algorithm.initialized && state.algorithm.history[8]==2);
    outputs[0]=&a;outputs[1]=&b;
    CHECK(api->process(&state,&context)==ORPHEUS_OK && fast[0]==2);
    const char* bad_id[]={"initial"};
    OrpheusValue bad_value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=NAN};
    OrpheusConfig bad_config={.param_ids=bad_id,.param_values=&bad_value,.param_count=1};
    CHECK(api->prepare(&state,&bad_config)==ORPHEUS_ERR_INVALID_ARG && state.algorithm.initialized && state.initial==10);
    CHECK(api->reset(&state)==ORPHEUS_OK && api->process(&state,&context)==ORPHEUS_OK && fast[0]==6);
    OrpheusValue got;CHECK(api->get_parameter(&state,"initial",&got)==ORPHEUS_OK && got.value.f32==10);
    for(unsigned port=1;port<=2;port++) {
        CHECK(api->reset(&state)==0);
        calls=0;fail_port=port;
        CHECK(api->process(&state,&context)==-77 && calls==port && state.algorithm.initialized
            && state.algorithm.history[8]==2 && state.algorithm.history[16]==2);
    }
    fail_port=0;
    CHECK(api->destroy(&state)==ORPHEUS_OK && state.algorithm.history==NULL);
    CHECK(api->create(&allocated,&config)==ORPHEUS_OK && allocated==&state);
    CHECK(api->prepare(allocated,&config)==ORPHEUS_OK && api->destroy(allocated)==ORPHEUS_OK);
    config.state_block=NULL;
    CHECK(api->create(&allocated,&config)==ORPHEUS_ERR_INVALID_ARG);
    return 0;
}
