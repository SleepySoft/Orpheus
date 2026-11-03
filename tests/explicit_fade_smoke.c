#include "orpheus_fade.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); return 1; } } while (0)
int main(void) {
    const OrpheusComponentInterface* iface=orpheus_get_interface();
    const char* ids[]={"execution_mode","channels","chan_map","current_gains","target_gains","coefficients","remaining_blocks"};
    OrpheusValue params[]={
        {.type=ORPHEUS_VALUE_INT,.value.i32=1}, {.type=ORPHEUS_VALUE_INT,.value.i32=4},
        {.type=ORPHEUS_VALUE_STRING,.value.str="0,1,-1,0"},
        {.type=ORPHEUS_VALUE_STRING,.value.str=".8,.7,.6,.5"},
        {.type=ORPHEUS_VALUE_STRING,.value.str=".3,.2,.1,.4"},
        {.type=ORPHEUS_VALUE_STRING,.value.str="1.01,.99,1.02,.98"},
        {.type=ORPHEUS_VALUE_STRING,.value.str="1,0,77,-123"}
    };
    OrpheusConfig config={.sample_rate=48000,.block_size=4,.channels=4,.param_count=7,.param_ids=ids,.param_values=params};
    FadeState state={0};
    CHECK(iface->prepare(&state,&config)==ORPHEUS_OK);
    uint32_t bits[]={0x80000000u,0x7fc12345u,0x7f800000u,0xff800000u,0,0x3e800000u};
    float input[16],low[16],output[16],expected[16],gains[4];
    for (unsigned n=0;n<4;++n) {
        gains[0]=.8f*powf(1.01f,(float)(n+1)); gains[1]=.2f;
        gains[2]=.6f*powf(1.02f,(float)(n+1)); gains[3]=.4f;
        for (unsigned c=0;c<4;++c) {
            unsigned i=n*4+c,r=c==1 ? 1 : 0;
            memcpy(&input[i],&bits[(n+c)%6],4); memcpy(&low[i],&bits[(n+c+2)%6],4);
            float high=input[i]-low[i];
            expected[i]=c==2 ? 0 : low[i]*gains[r+2]+high*gains[r];
        }
    }
    OrpheusBuffer dry={.data=input,.channels=4,.frame_capacity=4,.format=ORPHEUS_FORMAT_F32};
    OrpheusBuffer wet={.data=low,.channels=4,.frame_capacity=4,.format=ORPHEUS_FORMAT_F32};
    OrpheusBuffer out={.data=output,.channels=4,.frame_capacity=4,.format=ORPHEUS_FORMAT_F32};
    const OrpheusBuffer* inputs[]={&dry,&wet}; OrpheusBuffer* outputs[]={&out};
    OrpheusProcessContext context={.inputs=inputs,.outputs=outputs,.input_count=2,.output_count=1,.frame_count=4};
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && !memcmp(output,expected,sizeof(output)));
    CHECK(!memcmp(state.current_gains,gains,sizeof(gains)) && state.remaining_blocks[0]==0 &&
          state.remaining_blocks[2]==77 && state.remaining_blocks[3]==-123);
    FadeState before=state;
    context.frame_count=0;
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && !memcmp(&before,&state,sizeof(state)));
    context.frame_count=4;
    OrpheusValue freeze={.type=ORPHEUS_VALUE_BOOL,.value.b=true};
    CHECK(iface->set_parameter(&state,"freeze",&freeze)==ORPHEUS_OK);
    before=state;
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && !memcmp(&before,&state,sizeof(state)));
    CHECK(iface->reset(&state)==ORPHEUS_OK && !memcmp(&before,&state,sizeof(state)));
    inputs[1]=NULL;
    CHECK(iface->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG);
    inputs[1]=&wet; state.coefficients[0]=NAN;
    memcpy(expected,output,sizeof(expected));
    CHECK(iface->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG && !memcmp(expected,output,sizeof(output)));
    params[6].value.str="-2147483648,0,77,-123";
    CHECK(iface->prepare(&state,&config)==ORPHEUS_OK);
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && state.remaining_blocks[0]==INT32_MIN);
    params[2].value.str="0,1,2,0";
    CHECK(iface->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG);
    config.param_count=0;
    CHECK(iface->prepare(&state,&config)==ORPHEUS_OK && state.execution_mode==0);
    OrpheusValue request={.type=ORPHEUS_VALUE_FLOAT,.value.f32=1};
    CHECK(iface->set_parameter(&state,"fade",&request)==ORPHEUS_OK);
    float z[4]={0},front=state.front_gain,back=state.back_gain,b0=1-state.a1;
    for (unsigned n=0;n<4;++n) {
        front+=state.smoothing_coeff*(state.target_front-front);
        back+=state.smoothing_coeff*(state.target_back-back);
        for (unsigned c=0;c<4;++c) {
            unsigned i=n*4+c;
            input[i]=.01f*(i+1);
            float lpf=b0*input[i]+state.a1*z[c]; z[c]=lpf;
            expected[i]=lpf+(c<state.front_channels ? front : back)*(input[i]-lpf);
        }
    }
    context.input_count=1;
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && !memcmp(output,expected,sizeof(output)));
    return 0;
}
