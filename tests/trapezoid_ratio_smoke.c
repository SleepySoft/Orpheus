#include "orpheus_trapezoid_ratio.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do {if (!(x)) {fprintf(stderr,"%d: %s\n",__LINE__,#x);return 1;}} while (0)
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();TrapezoidRatioState state={0};
    const char* ids[]={"channels","bins","batches"};
    OrpheusValue values[]={{.type=ORPHEUS_VALUE_INT,.value.i32=2},{.type=ORPHEUS_VALUE_INT,.value.i32=3},{.type=ORPHEUS_VALUE_INT,.value.i32=2}};
    OrpheusConfig config={.sample_rate=48000,.block_size=1,.param_ids=ids,.param_values=values,.param_count=3};
    CHECK(api->prepare(&state,&config)==ORPHEUS_OK);
    float input[2][12],output[12],want[12],previous[2][6]={{0}},last[2][6]={{0}};
    OrpheusBuffer buffers[2];
    for(unsigned p=0;p<2;++p) buffers[p]=(OrpheusBuffer){.data=input[p],.channels=2,.frame_count=6,.frame_capacity=6,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer out={.data=output,.channels=2,.frame_capacity=6,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    const OrpheusBuffer* inputs[]={buffers,buffers+1};OrpheusBuffer* outputs[]={&out};
    OrpheusProcessContext context={.inputs=inputs,.outputs=outputs,.input_count=2,.output_count=1,.frame_count=1};
    for(unsigned call=0;call<9;++call) {
        float alpha=call<3 ? 0.7f : call<6 ? 0.0f : 1.0f,epsilon=call<6 ? 0.001f : 0.0f;
        OrpheusValue value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=alpha};
        CHECK(api->set_parameter(&state,"alpha",&value)==ORPHEUS_OK);
        value.value.f32=epsilon;CHECK(api->set_parameter(&state,"epsilon",&value)==ORPHEUS_OK);
        if(call==5) {
            CHECK(api->reset(&state)==ORPHEUS_OK);memset(previous,0,sizeof(previous));memset(last,0,sizeof(last));
        }
        for(unsigned p=0;p<2;++p) for(unsigned i=0;i<12;++i) input[p][i]=((int)(i*7+call*3+p*11)%23-11)*0.125f;
        if(call==8) {
            const uint32_t bits[]={0,0x80000000u,0x7f800000u,0xff800000u,0x7fc12345u,0x7f812345u};
            for(unsigned p=0;p<2;++p) for(unsigned i=0;i<12;++i) memcpy(input[p]+i,&bits[(i+p)%6],4);
        }
        float saved[24];memcpy(saved,state.history,sizeof(saved));out.frame_capacity=5;
        CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG && !memcmp(saved,state.history,sizeof(saved)));
        out.frame_capacity=6;out.data=input[0];CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG);out.data=output;
        context.frame_count=0;CHECK(api->process(&state,&context)==ORPHEUS_OK && !memcmp(saved,state.history,sizeof(saved)));context.frame_count=1;
        float factor=(1-alpha)*0.5f;
        for(unsigned h=0;h<2;++h) for(unsigned i=0;i<6;++i) {
            float smoothed[2];
            for(unsigned p=0;p<2;++p) {
                float current=input[p][h*6+i];
                volatile float feed=(current+previous[p][i])*factor,feedback=last[p][i]*alpha;
                smoothed[p]=feed+feedback;previous[p][i]=current;last[p][i]=smoothed[p];
            }
            volatile float denominator=smoothed[1]+epsilon;want[h*6+i]=smoothed[0]/denominator;
        }
        outputs[0]=call==2 ? NULL : &out;CHECK(api->process(&state,&context)==ORPHEUS_OK);
        if(outputs[0]) CHECK(out.frame_count==6 && !memcmp(output,want,sizeof(output)));
        for(unsigned p=0;p<2;++p) {
            CHECK(!memcmp(state.history+p*12,previous[p],sizeof(previous[p])));
            CHECK(!memcmp(state.history+p*12+6,last[p],sizeof(last[p])));
        }
        outputs[0]=&out;
    }
    OrpheusValue value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=NAN};
    CHECK(api->set_parameter(&state,"alpha",&value)==ORPHEUS_ERR_INVALID_ARG);
    value.value.f32=-1;CHECK(api->set_parameter(&state,"epsilon",&value)==ORPHEUS_ERR_INVALID_ARG);
    TrapezoidRatioState saved=state;values[1].value.i32=0;
    CHECK(api->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&saved,&state,sizeof(state)));
    CHECK(api->destroy(&state)==ORPHEUS_OK);
    void* allocated=NULL;config.state_block=&state;
    CHECK(api->create(&allocated,&config)==ORPHEUS_OK && allocated==&state && !state.owns_memory);
    CHECK(api->destroy(allocated)==ORPHEUS_OK);config.state_block=NULL;
    CHECK(api->create(&allocated,&config)==ORPHEUS_OK && ((TrapezoidRatioState*)allocated)->owns_memory);
    CHECK(api->destroy(allocated)==ORPHEUS_OK);
    return 0;
}
