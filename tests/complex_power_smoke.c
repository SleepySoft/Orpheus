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
static int maximum_payload(const OrpheusComponentInterface* api) {
    orpheus_builtin_complex_power_State state;
    const char* keys[]={"channels","bins","batches"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=1},
        {.type=ORPHEUS_VALUE_INT,.value.i32=65536},{.type=ORPHEUS_VALUE_INT,.value.i32=64}};
    OrpheusConfig config={.state_block=&state,.param_ids=keys,.param_values=params,.param_count=3};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0);
    const unsigned points=65536*64;
    float* input=(float*)malloc((size_t)points*2*sizeof(float));
    float* power=(float*)malloc((size_t)points*sizeof(float));
    float energy[64];
    CHECK(input && power);
    for(unsigned i=0;i<points;i++) {input[i*2]=3;input[i*2+1]=4;}
    OrpheusBuffer in={.data=input,.channels=1,.frame_count=points*2,.frame_capacity=points*2,
        .format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer a={.data=power,.channels=1,.frame_capacity=points,
        .format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer b={.data=energy,.channels=1,.frame_capacity=64,
        .format=ORPHEUS_FORMAT_F32,.interleaved=true};
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&a,&b};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=2,.frame_count=8};
    CHECK(api->process(handle,&ctx)==0 && a.frame_count==points && b.frame_count==64);
    for(unsigned i=0;i<points;i++) CHECK(power[i]==25);
    for(unsigned i=0;i<64;i++) CHECK(energy[i]==log10f(65536.0f*25)*10);
    params[1].value.i32=65537;
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && state.bins==65536);
    free(input);free(power);
    CHECK(api->destroy(handle)==0);
    return 0;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    const uint32_t bits[]={0,0x80000000u,0x7f800000u,0xff800000u,0x7fc12345u,0x7f812345u,0x3f99999au,1};
    float input[32],power[16],energy[4],want_p[16],want_e[4];
    const char* ids[]={"channels","bins","batches","epsilon"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=2},{.type=ORPHEUS_VALUE_INT,.value.i32=4},
                          {.type=ORPHEUS_VALUE_INT,.value.i32=2},{.type=ORPHEUS_VALUE_FLOAT,.value.f32=2.22044605e-16f}};
    OrpheusConfig config={.sample_rate=48000,.block_size=1,.param_ids=ids,.param_values=params,.param_count=4};
    OrpheusBuffer in={.data=input,.channels=2,.frame_count=16,.frame_capacity=16,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer a={.data=power,.channels=2,.frame_capacity=8,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer b={.data=energy,.channels=2,.frame_capacity=2,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&a,&b};
    OrpheusContinuation continuation={.emit=emit};
    OrpheusProcessContext context={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=2,.frame_count=1,
        .continuation=&continuation};
    orpheus_builtin_complex_power_State state;
    config.state_block=&state;void* allocated=NULL;
    CHECK(api->create(&allocated,&config)==ORPHEUS_OK && allocated==&state);
    for(unsigned kind=0;kind<4;++kind) {
        for(unsigned i=0;i<32;++i) {
            if(kind==1) memcpy(input+i,&bits[i%8],4);
            else input[i]=kind==2 ? -0.0f : ((int)i-16)*0.03125f;
        }
        if(kind==3) input[0]=1e10f;
        params[3].value.f32=kind==2 ? 0.0f : 2.22044605e-16f;
        CHECK(api->prepare(&state,&config)==ORPHEUS_OK);
        for(unsigned h=0;h<2;++h) for(unsigned c=0;c<2;++c) {
            float sum=-0.0f;
            for(unsigned k=0;k<4;++k) {
                unsigned index=((h*4+k)*2)*2+c;
                volatile float real=input[index]*input[index],imaginary=input[index+2]*input[index+2];
                float value=real+imaginary;want_p[(h*4+k)*2+c]=value;sum+=value;
            }
            want_e[h*2+c]=log10f(sum+params[3].value.f32)*10.0f;
        }
        calls=0;
        CHECK(api->process(&state,&context)==ORPHEUS_OK && a.frame_count==8 && b.frame_count==2);
        CHECK(calls==2 && ports[0]==1 && ports[1]==2);
        CHECK(!memcmp(power,want_p,sizeof(power)) && !memcmp(energy,want_e,sizeof(energy)));
        b.frame_capacity=1;
        CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG && !memcmp(power,want_p,sizeof(power)));
        b.frame_capacity=2;b.data=power;CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG);b.data=energy;
        a.data=input;CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG);a.data=power;
        in.frame_count=15;CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG);in.frame_count=16;
        in.interleaved=false;CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG);in.interleaved=true;
        outputs[0]=NULL;CHECK(api->process(&state,&context)==ORPHEUS_OK);outputs[0]=&a;
        outputs[1]=NULL;CHECK(api->process(&state,&context)==ORPHEUS_OK);outputs[1]=&b;
        context.frame_count=0;CHECK(api->process(&state,&context)==ORPHEUS_OK);context.frame_count=1;
        CHECK(api->reset(&state)==ORPHEUS_OK);
    }
    fail_port=1;calls=0;
    CHECK(api->process(&state,&context)==-77 && calls==1);
    fail_port=2;calls=0;
    CHECK(api->process(&state,&context)==-77 && calls==2);fail_port=0;
    params[3].value.f32=-1;CHECK(api->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG);
    params[3].value.f32=NAN;CHECK(api->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->destroy(allocated)==ORPHEUS_OK);
    config.param_count=0;config.state_block=&state;
    CHECK(api->create(&allocated,&config)==ORPHEUS_OK && allocated==&state);
    CHECK(api->prepare(allocated,&config)==ORPHEUS_OK && api->destroy(allocated)==ORPHEUS_OK);
    config.state_block=NULL;
    CHECK(api->create(&allocated,&config)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(maximum_payload(api)==0);
    return 0;
}
