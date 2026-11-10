#include "component_state.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if (!(x)) {fprintf(stderr,"%d: %s\n",__LINE__,#x);return 1;}} while (0)
static unsigned calls;
static int fail;
static int emit(void* user,uint32_t port,const OrpheusDelivery* delivery) {
    (void)user;
    if(port!=1 || !delivery) return ORPHEUS_ERR_INVALID_ARG;
    calls++;
    return fail;
}
static int maximum_shapes(const OrpheusComponentInterface* api) {
    const char* ids[]={"channels","bins","batches","alpha","epsilon","division_mode"};
    for(unsigned shape=0;shape<2;shape++) {
        unsigned channels=shape ? 1 : 64,bins=65536,batches=shape ? 64 : 1;
        size_t points=(size_t)channels*bins,count=points*batches;
        float* numerator=malloc(count*sizeof(float));float* denominator=malloc(count*sizeof(float));
        float* output=malloc(count*sizeof(float));CHECK(numerator && denominator && output);
        for(size_t i=0;i<count;i++) {
            numerator[i]=((int)(i%257)-128)*.03125f;
            denominator[i]=((int)(i%127)+1)*.0625f;
        }
        OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=channels},
            {.type=ORPHEUS_VALUE_INT,.value.i32=bins},{.type=ORPHEUS_VALUE_INT,.value.i32=batches},
            {.type=ORPHEUS_VALUE_FLOAT,.value.f32=.9f},{.type=ORPHEUS_VALUE_FLOAT,.value.f32=.01f},
            {.type=ORPHEUS_VALUE_INT,.value.i32=0}};
        orpheus_builtin_trapezoid_ratio_State state;void* handle=NULL;
        OrpheusConfig config={.sample_rate=48000,.block_size=8,.state_block=&state,
            .param_ids=ids,.param_values=params,.param_count=6};
        OrpheusBuffer a={.data=numerator,.channels=channels,.frame_count=bins*batches,.frame_capacity=bins*batches,
            .format=ORPHEUS_FORMAT_F32,.interleaved=true};
        OrpheusBuffer b=a;b.data=denominator;
        OrpheusBuffer out=a;out.data=output;out.frame_count=0;
        const OrpheusBuffer* inputs[]={&a,&b};OrpheusBuffer* outputs[]={&out};
        OrpheusContinuation continuation={.emit=emit};
        OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=2,.output_count=1,
            .frame_count=8,.continuation=&continuation};
        CHECK(api->create(&handle,&config)==0);
        for(unsigned mode=0;mode<2;mode++) {
            params[5].value.i32=mode;
            CHECK(api->prepare(handle,&config)==0);
            calls=0;ctx.trigger_input=mode;
            CHECK(api->process(handle,&ctx)==0 && calls==1 && out.frame_count==bins*batches);
            float* sources[]={numerator,denominator};
            const float factor=(1.0f-.9f)*.5f;
            for(size_t i=0;i<points;i++) {
                float previous[]={0,0},last[]={0,0};
                for(unsigned h=0;h<batches;h++) {
                    size_t index=h*points+i;
                    for(unsigned p=0;p<2;p++) {
                        float current=sources[p][index],pair=current+previous[p];
                        volatile float feed=pair*factor,feedback=last[p]*.9f;
                        last[p]=feed+feedback;previous[p]=current;
                    }
                    volatile float divisor=last[1]+.01f,reciprocal=1.0f/divisor;
                    float value=mode ? reciprocal*last[0] : last[0]/divisor;
                    CHECK(!memcmp(&value,output+index,4));
                }
                for(unsigned p=0;p<2;p++) {
                    CHECK(!memcmp(previous+p,state.algorithm.history+p*points*2+i,4));
                    CHECK(!memcmp(last+p,state.algorithm.history+(p*2+1)*points+i,4));
                }
            }
        }
        params[1].value.i32=65537;
        CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && state.algorithm.count==points);
        CHECK(api->destroy(handle)==0);
        free(numerator);free(denominator);free(output);
    }
    return 0;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();orpheus_builtin_trapezoid_ratio_State state;
    CHECK(maximum_shapes(api)==0);
    const char* ids[]={"channels","bins","batches"};
    OrpheusValue values[]={{.type=ORPHEUS_VALUE_INT,.value.i32=2},{.type=ORPHEUS_VALUE_INT,.value.i32=3},{.type=ORPHEUS_VALUE_INT,.value.i32=2}};
    OrpheusConfig config={.sample_rate=48000,.block_size=1,.param_ids=ids,.param_values=values,.param_count=3};
    void* allocated=NULL;config.state_block=&state;
    CHECK(api->create(&allocated,&config)==0 && api->prepare(&state,&config)==ORPHEUS_OK);
    CHECK(state.division_mode==0 && api->get_descriptor()->param_count==6);
    float input[2][12],output[12],want[12],previous[2][6]={{0}},last[2][6]={{0}};
    OrpheusBuffer buffers[2];
    for(unsigned p=0;p<2;++p) buffers[p]=(OrpheusBuffer){.data=input[p],.channels=2,.frame_count=6,.frame_capacity=6,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer out={.data=output,.channels=2,.frame_capacity=6,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    const OrpheusBuffer* inputs[]={buffers,buffers+1};OrpheusBuffer* outputs[]={&out};
    OrpheusContinuation continuation={.emit=emit};
    OrpheusProcessContext context={.inputs=inputs,.outputs=outputs,.input_count=2,.output_count=1,.frame_count=8,
        .continuation=&continuation};
    bool order_difference=false;
    for(unsigned call=0;call<18;++call) {
        unsigned phase=call%9,mode=call/9;
        OrpheusValue order={.type=ORPHEUS_VALUE_INT,.value.i32=mode},readback;
        CHECK(api->set_parameter(&state,"division_mode",&order)==ORPHEUS_OK);
        CHECK(api->get_parameter(&state,"division_mode",&readback)==ORPHEUS_OK &&
              readback.type==ORPHEUS_VALUE_INT && readback.value.i32==(int32_t)mode);
        float alpha=phase<3 ? 0.7f : phase<6 ? 0.0f : 1.0f,epsilon=phase<6 ? 0.001f : 0.0f;
        OrpheusValue value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=alpha};
        CHECK(api->set_parameter(&state,"alpha",&value)==ORPHEUS_OK);
        value.value.f32=epsilon;CHECK(api->set_parameter(&state,"epsilon",&value)==ORPHEUS_OK);
        if(phase==5) {
            CHECK(api->reset(&state)==ORPHEUS_OK);memset(previous,0,sizeof(previous));memset(last,0,sizeof(last));
        }
        for(unsigned p=0;p<2;++p) for(unsigned i=0;i<12;++i) input[p][i]=((int)(i*7+call*3+p*11)%23-11)*0.125f;
        if(phase==8) {
            const uint32_t bits[]={0,0x80000000u,0x7f800000u,0xff800000u,0x7fc12345u,0x7f812345u};
            for(unsigned p=0;p<2;++p) for(unsigned i=0;i<12;++i) memcpy(input[p]+i,&bits[(i+p)%6],4);
        }
        float saved[24];memcpy(saved,state.algorithm.history,sizeof(saved));out.frame_capacity=5;
        CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG && !memcmp(saved,state.algorithm.history,sizeof(saved)));
        out.frame_capacity=6;out.data=input[0];CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG);out.data=output;
        inputs[1]=NULL;
        CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG && !memcmp(saved,state.algorithm.history,sizeof(saved)));
        inputs[1]=buffers+1;
        context.frame_count=0;calls=0;
        CHECK(api->process(&state,&context)==ORPHEUS_OK && !calls && !memcmp(saved,state.algorithm.history,sizeof(saved)));context.frame_count=8;
        float factor=(1-alpha)*0.5f;
        for(unsigned h=0;h<2;++h) for(unsigned i=0;i<6;++i) {
            float smoothed[2];
            for(unsigned p=0;p<2;++p) {
                float current=input[p][h*6+i];
                volatile float feed=(current+previous[p][i])*factor,feedback=last[p][i]*alpha;
                smoothed[p]=feed+feedback;previous[p][i]=current;last[p][i]=smoothed[p];
            }
            volatile float denominator=smoothed[1]+epsilon,reciprocal=1.0f/denominator;
            float direct=smoothed[0]/denominator,indirect=reciprocal*smoothed[0];
            if(isfinite(direct) && isfinite(indirect) && memcmp(&direct,&indirect,4)) order_difference=true;
            want[h*6+i]=mode ? indirect : direct;
        }
        context.trigger_input=call%2;
        outputs[0]=phase==2 ? NULL : &out;CHECK(api->process(&state,&context)==ORPHEUS_OK && calls==1);
        if(outputs[0]) CHECK(out.frame_count==6 && !memcmp(output,want,sizeof(output)));
        for(unsigned p=0;p<2;++p) {
            CHECK(!memcmp(state.algorithm.history+p*12,previous[p],sizeof(previous[p])));
            CHECK(!memcmp(state.algorithm.history+p*12+6,last[p],sizeof(last[p])));
        }
        CHECK(order_difference);
        outputs[0]=&out;
    }
    OrpheusValue value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=NAN};
    CHECK(api->set_parameter(&state,"alpha",&value)==ORPHEUS_ERR_INVALID_ARG);
    value.value.f32=-1;CHECK(api->set_parameter(&state,"epsilon",&value)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->set_parameter(&state,"division_mode",&value)==ORPHEUS_ERR_INVALID_ARG);
    value.type=ORPHEUS_VALUE_INT;value.value.i32=-1;
    CHECK(api->set_parameter(&state,"division_mode",&value)==ORPHEUS_ERR_INVALID_ARG);
    value.value.i32=2;CHECK(api->set_parameter(&state,"division_mode",&value)==ORPHEUS_ERR_INVALID_ARG);
    value.type=ORPHEUS_VALUE_FLOAT;value.value.f32=FLT_MAX;
    CHECK(api->set_parameter(&state,"epsilon",&value)==0 && state.epsilon==FLT_MAX);
    value.value.f32=INFINITY;CHECK(api->set_parameter(&state,"epsilon",&value)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->reset(&state)==0);
    for(unsigned p=0;p<2;p++) for(unsigned i=0;i<12;i++) input[p][i]=2.0f;
    calls=0;fail=-77;CHECK(api->process(&state,&context)==-77 && calls==1);fail=0;
    CHECK(state.algorithm.history[0]==2.0f && state.algorithm.history[12]==2.0f);
    orpheus_builtin_trapezoid_ratio_State saved=state;values[1].value.i32=0;
    CHECK(api->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&saved,&state,sizeof(state)));
    const char* mode_id="division_mode";
    value.type=ORPHEUS_VALUE_INT;value.value.i32=2;
    OrpheusConfig mode_config={.param_ids=&mode_id,.param_values=&value,.param_count=1};
    CHECK(api->prepare(&state,&mode_config)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&saved,&state,sizeof(state)));
    value.value.i32=1;CHECK(api->prepare(&state,&mode_config)==ORPHEUS_OK && state.division_mode==1);
    CHECK(api->reset(&state)==ORPHEUS_OK && state.division_mode==1);
    CHECK(api->destroy(&state)==ORPHEUS_OK);
    values[1].value.i32=3;
    CHECK(api->create(&allocated,&config)==ORPHEUS_OK && allocated==&state);
    CHECK(api->destroy(allocated)==ORPHEUS_OK);config.state_block=NULL;
    CHECK(api->create(&allocated,&config)==ORPHEUS_ERR_INVALID_ARG);
    return 0;
}
