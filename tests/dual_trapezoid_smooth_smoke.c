#include "orpheus_dual_trapezoid_smooth.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); return 1; } } while (0)
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
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
    OrpheusProcessContext context={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=2,.frame_count=1};
    DualTrapezoidSmoothState state={0};
    CHECK(api->prepare(&state,&config)==ORPHEUS_OK);
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
        float saved[24];memcpy(saved,state.history,sizeof(saved));
        b.frame_capacity=11;
        CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG && !memcmp(saved,state.history,sizeof(saved)));
        b.frame_capacity=12;b.data=fast;
        CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG && !memcmp(saved,state.history,sizeof(saved)));b.data=slow;
        a.data=input;CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG);a.data=fast;
        context.frame_count=0;CHECK(api->process(&state,&context)==ORPHEUS_OK && !memcmp(saved,state.history,sizeof(saved)));context.frame_count=1;
        for(unsigned h=0;h<3;++h) for(unsigned i=0;i<8;++i) {
            unsigned index=h*8+i;float pair=input[index]+previous[i];
            for(unsigned p=0;p<2;++p) {
                volatile float feed=pair*((1.0f-alphas[p])*0.5f),feedback=hist[p][i]*alphas[p];
                expected[p][index]=feed+feedback;hist[p][i]=expected[p][index];
            }
            previous[i]=input[index];
        }
        outputs[0]=call==1 ? NULL : &a;outputs[1]=call==2 ? NULL : &b;
        CHECK(api->process(&state,&context)==ORPHEUS_OK);
        if(outputs[0]) CHECK(a.frame_count==12 && !memcmp(fast,expected[0],sizeof(fast)));
        if(outputs[1]) CHECK(b.frame_count==12 && !memcmp(slow,expected[1],sizeof(slow)));
        CHECK(!memcmp(state.history,previous,sizeof(previous)) &&
              !memcmp(state.history+8,hist[0],sizeof(previous)) && !memcmp(state.history+16,hist[1],sizeof(previous)));
        outputs[0]=&a;outputs[1]=&b;
    }
    OrpheusValue value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=NAN};
    CHECK(api->set_parameter(&state,"fast_alpha",&value)==ORPHEUS_ERR_INVALID_ARG);
    value.value.f32=-0.1f;CHECK(api->set_parameter(&state,"slow_alpha",&value)==ORPHEUS_ERR_INVALID_ARG);
    value.value.f32=INFINITY;CHECK(api->set_parameter(&state,"slow_alpha",&value)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->destroy(&state)==ORPHEUS_OK && state.history==NULL);
    config.state_block=&state;void* allocated=NULL;
    CHECK(api->create(&allocated,&config)==ORPHEUS_OK && allocated==&state && !state.owns_memory);
    CHECK(api->prepare(allocated,&config)==ORPHEUS_OK && api->destroy(allocated)==ORPHEUS_OK);
    config.state_block=NULL;
    CHECK(api->create(&allocated,&config)==ORPHEUS_OK && ((DualTrapezoidSmoothState*)allocated)->owns_memory);
    CHECK(api->prepare(allocated,&config)==ORPHEUS_OK && api->destroy(allocated)==ORPHEUS_OK);
    return 0;
}
