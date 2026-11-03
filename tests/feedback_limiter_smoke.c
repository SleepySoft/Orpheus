#include "orpheus_limiter.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); return 1; } } while (0)
int main(void) {
    const OrpheusComponentInterface* iface=orpheus_get_interface();
    const char* ids[]={"mode","channels","feedback_attack"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_STRING,.value.str="feedback"},
                          {.type=ORPHEUS_VALUE_INT,.value.i32=32},
                          {.type=ORPHEUS_VALUE_STRING,.value.str=""}};
    OrpheusConfig config={.sample_rate=48000,.block_size=4,.channels=32,.param_count=3,.param_ids=ids,.param_values=params};
    LimiterState state={0};
    CHECK(iface->prepare(&state,&config)==ORPHEUS_OK && state.mode==2 && state.env[31]==1);
    float input[128], actual[128], expected[128], gains[32];
    for (unsigned c=0;c<32;++c) {
        float gain=1;
        for (unsigned n=0;n<4;++n) {
            float x=c==31 ? 10 : .125f;
            input[n*32+c]=x;
            gain=fminf(fmaxf((.0237f+1)-fmaxf(.01185f*gain*fabsf(x),.0237f),.31623f)*(gain*1.00024f),1);
            expected[n*32+c]=x*gain;
        }
        gains[c]=gain;
    }
    OrpheusBuffer in={.data=input,.channels=32,.frame_capacity=4,.format=ORPHEUS_FORMAT_F32};
    OrpheusBuffer out={.data=actual,.channels=32,.frame_capacity=4,.format=ORPHEUS_FORMAT_F32};
    const OrpheusBuffer* inputs[]={&in}; OrpheusBuffer* outputs[]={&out};
    OrpheusProcessContext context={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,.frame_count=4};
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && !memcmp(actual,expected,sizeof(actual)));
    CHECK(!memcmp(state.env,gains,sizeof(gains)) && state.active_mask==INT32_MIN);
    context.frame_count=0;
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && state.active_mask==INT32_MIN && !memcmp(state.env,gains,sizeof(gains)));
    context.frame_count=4;
    OrpheusValue disabled={.type=ORPHEUS_VALUE_BOOL,.value.b=true};
    CHECK(iface->set_parameter(&state,"disabled",&disabled)==ORPHEUS_OK);
    uint32_t special[]={0x80000000u,0x7fc12345u};
    memcpy(input,special,sizeof(special));
    CHECK(iface->process(&state,&context)==ORPHEUS_OK && !memcmp(actual,input,sizeof(input)) &&
          !memcmp(state.env,gains,sizeof(gains)) && state.active_mask==0);
    disabled.value.b=false;
    CHECK(iface->set_parameter(&state,"disabled",&disabled)==ORPHEUS_OK);
    memset(actual,0x34,sizeof(actual));
    memcpy(expected,actual,sizeof(actual));
    state.feedback_attack[0]=NAN;
    CHECK(iface->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG && !memcmp(actual,expected,sizeof(actual)) &&
          !memcmp(state.env,gains,sizeof(gains)));
    state.feedback_attack[0]=.0237f;
    OrpheusValue set={.type=ORPHEUS_VALUE_INT,.value.i32=2};
    CHECK(iface->set_parameter(&state,"parameter_set",&set)==ORPHEUS_OK);
    CHECK(iface->reset(&state)==ORPHEUS_OK && state.parameter_set==2 && !state.disabled && state.active_mask==0);
    for (unsigned c=0;c<32;++c) CHECK(state.env[c]==1);
    set.value.i32=3;
    CHECK(iface->set_parameter(&state,"parameter_set",&set)==ORPHEUS_ERR_INVALID_ARG);
    const char* bad[]={"1,2","nan","1,2,3x"};
    for (unsigned i=0;i<3;++i) {
        params[2].value.str=bad[i];
        CHECK(iface->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG);
    }
    params[2].value.str=""; params[1].value.i32=33;
    CHECK(iface->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG);
    params[1].value.i32=2; config.channels=2;
    ids[2]="threshold_db"; params[2]=(OrpheusValue){.type=ORPHEUS_VALUE_FLOAT,.value.f32=-50};
    in.channels=out.channels=2;
    for (unsigned i=0;i<8;++i) input[i]=.1f*(i+1);
    for (unsigned mode=0;mode<2;++mode) {
        params[0].value.str=mode ? "per_channel" : "shared";
        CHECK(iface->prepare(&state,&config)==ORPHEUS_OK && state.mode==mode && state.env[0]==0);
        float envelope[2]={0};
        for (unsigned n=0;n<4;++n) for (unsigned c=0;c<2;++c) {
            float x=input[n*2+c], magnitude=fabsf(x), *env=&envelope[mode ? c : 0];
            float attack=mode ? state.attack_coeff_per_channel[c] : state.attack_coeff;
            float release=mode ? state.release_coeff_per_channel[c] : state.release_coeff;
            *env+=(magnitude>*env ? attack : release)*(magnitude-*env);
            float gain=*env>state.threshold_linear && *env>0 ? state.threshold_linear / *env : 1;
            if (mode && gain<1) gain=fmaxf(state.k1[c]*state.threshold_linear / *env,state.max_attack[c]);
            expected[n*2+c]=x*gain;
        }
        CHECK(iface->process(&state,&context)==ORPHEUS_OK && !memcmp(actual,expected,8*sizeof(float)));
        CHECK(iface->reset(&state)==ORPHEUS_OK && state.env[0]==0);
    }
    return 0;
}
