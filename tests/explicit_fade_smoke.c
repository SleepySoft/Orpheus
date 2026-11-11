#include "component_state.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); return 1; } } while (0)
typedef orpheus_builtin_fade_State FadeState;
static int fail_delivery(void* user,uint32_t port,const OrpheusDelivery* delivery) {
    unsigned* calls=(unsigned*)user;
    (*calls)++;
    return port==1 && delivery && delivery->buffer && delivery->buffer->frame_count==4 ?
        ORPHEUS_ERR_PROCESSING : ORPHEUS_ERR_INVALID_ARG;
}
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
    config.state_block=&state;
    void* raw=NULL;
    CHECK(iface->create(&raw,&config)==ORPHEUS_OK && raw==&state);
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
    OrpheusBuffer dry={.data=input,.channels=4,.frame_count=4,.frame_capacity=4,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer wet={.data=low,.channels=4,.frame_count=4,.frame_capacity=4,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer out={.data=output,.channels=4,.frame_capacity=4,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    const OrpheusBuffer* inputs[]={&dry,&wet}; OrpheusBuffer* outputs[]={&out};
    OrpheusProcessContext context={.inputs=inputs,.outputs=outputs,.input_count=2,.output_count=1,.frame_count=4};
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && !memcmp(output,expected,sizeof(output)));
    CHECK(!memcmp(state.algorithm.current_gains,gains,sizeof(gains)) && state.algorithm.remaining_blocks[0]==0 &&
          state.algorithm.remaining_blocks[2]==77 && state.algorithm.remaining_blocks[3]==-123);
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
    inputs[1]=&wet; state.algorithm.coefficients[0]=NAN;
    memcpy(expected,output,sizeof(expected));
    CHECK(iface->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG && !memcmp(expected,output,sizeof(output)));
    params[6].value.str="-2147483648,0,77,-123";
    CHECK(iface->prepare(&state,&config)==ORPHEUS_OK);
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && state.algorithm.remaining_blocks[0]==INT32_MIN);
    state.algorithm.coefficients[0]=1;
    before=state;wet.frame_count=3;
    CHECK(iface->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&before,&state,sizeof(state)));
    wet.frame_count=4;out.data=low+1;
    CHECK(iface->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&before,&state,sizeof(state)));
    out.data=output;
    unsigned calls=0;
    OrpheusContinuation continuation={.user=&calls,.emit=fail_delivery};
    context.continuation=&continuation;
    CHECK(iface->process(&state,&context)==ORPHEUS_ERR_PROCESSING && calls==1 &&
          !memcmp(state.algorithm.remaining_blocks,before.algorithm.remaining_blocks,sizeof(state.algorithm.remaining_blocks)) &&
          memcmp(state.algorithm.current_gains,before.algorithm.current_gains,sizeof(state.algorithm.current_gains)));
    context.continuation=NULL;
    params[2].value.str="0,1,2,0";
    CHECK(iface->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG);
    config.param_count=0;
    CHECK(iface->prepare(&state,&config)==ORPHEUS_OK && state.execution_mode==0);
    OrpheusValue request={.type=ORPHEUS_VALUE_FLOAT,.value.f32=1};
    CHECK(iface->set_parameter(&state,"fade",&request)==ORPHEUS_OK);
    float z[4]={0},front=state.algorithm.front_gain,back=state.algorithm.back_gain,b0=1-state.algorithm.a1;
    for (unsigned n=0;n<4;++n) {
        front+=state.algorithm.smoothing_coeff*(state.algorithm.target_front-front);
        back+=state.algorithm.smoothing_coeff*(state.algorithm.target_back-back);
        for (unsigned c=0;c<4;++c) {
            unsigned i=n*4+c;
            input[i]=.01f*(i+1);
            float lpf=b0*input[i]+state.algorithm.a1*z[c]; z[c]=lpf;
            expected[i]=lpf+(c<state.front_channels ? front : back)*(input[i]-lpf);
        }
    }
    context.input_count=1;
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && !memcmp(output,expected,sizeof(output)));
    front=state.algorithm.front_gain;back=state.algorithm.back_gain;
    CHECK(iface->reset(&state)==ORPHEUS_OK && state.algorithm.front_gain==front && state.algorithm.back_gain==back);
    for(unsigned c=0;c<32;c++) CHECK(state.algorithm.z[c]==0);
    before=state;context.frame_count=0;
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && !memcmp(&before,&state,sizeof(state)));
    context.frame_count=4;out.data=input+1;
    CHECK(iface->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&before,&state,sizeof(state)));
    out.data=input;
    CHECK(iface->process(&state,&context)==ORPHEUS_OK);
    request.value.f32=NAN;
    CHECK(iface->set_parameter(&state,"fade",&request)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(iface->destroy(&state)==ORPHEUS_OK);
    return 0;
}
