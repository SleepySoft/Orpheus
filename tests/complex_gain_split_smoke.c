#include "orpheus_complex_gain_split.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); return 1; } } while (0)
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    const uint32_t bits[]={0,0x80000000u,0x7f800000u,0xff800000u,0x7fc12345u,0x7f812345u,0x3f99999au,0x3eaaaaabu};
    float input[32],gain[16],impulse[32],reverb[32],want_impulse[32],want_reverb[32];
    for(unsigned i=0;i<32;++i) memcpy(input+i,&bits[i%8],4);
    for(unsigned i=0;i<16;++i) memcpy(gain+i,&bits[(i+3)%8],4);
    memcpy(input,&bits[6],4); memcpy(gain,&bits[7],4);
    const char* ids[]={"channels","elements"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=2},{.type=ORPHEUS_VALUE_INT,.value.i32=8}};
    OrpheusConfig config={.sample_rate=48000,.block_size=1,.param_ids=ids,.param_values=params,.param_count=2};
    ComplexGainSplitState state={0};
    CHECK(api->prepare(&state,&config)==ORPHEUS_OK);
    OrpheusBuffer in={.data=input,.channels=2,.frame_count=16,.frame_capacity=16,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer gains={.data=gain,.channels=2,.frame_count=8,.frame_capacity=8,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer a={.data=impulse,.channels=2,.frame_capacity=16,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer b={.data=reverb,.channels=2,.frame_capacity=16,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    const OrpheusBuffer* inputs[]={&in,&gains}; OrpheusBuffer* outputs[]={&a,&b};
    OrpheusProcessContext context={.inputs=inputs,.outputs=outputs,.input_count=2,.output_count=2,.frame_count=1};
    for(unsigned n=0;n<8;++n) for(unsigned part=0;part<2;++part) for(unsigned c=0;c<2;++c) {
        unsigned i=(n*2+part)*2+c;
        volatile float product=gain[n*2+c]*input[i];
        want_reverb[i]=product; want_impulse[i]=input[i]-product;
    }
    CHECK(api->process(&state,&context)==ORPHEUS_OK && a.frame_count==16 && b.frame_count==16);
    CHECK(!memcmp(impulse,want_impulse,sizeof(impulse)) && !memcmp(reverb,want_reverb,sizeof(reverb)));
    b.frame_capacity=15;
    CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG && !memcmp(impulse,want_impulse,sizeof(impulse)));
    b.frame_capacity=16; b.data=input; CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG);
    b.data=impulse; CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG); b.data=reverb;
    gains.frame_count=7; CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG); gains.frame_count=8;
    in.interleaved=false; CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG); in.interleaved=true;
    outputs[0]=NULL; CHECK(api->process(&state,&context)==ORPHEUS_OK); outputs[0]=&a;
    outputs[1]=NULL; CHECK(api->process(&state,&context)==ORPHEUS_OK); outputs[1]=&b;
    inputs[1]=NULL; CHECK(api->process(&state,&context)==ORPHEUS_ERR_INVALID_ARG); inputs[1]=&gains;
    context.frame_count=0; CHECK(api->process(&state,&context)==ORPHEUS_OK); context.frame_count=1;
    CHECK(api->reset(&state)==ORPHEUS_OK && api->process(&state,&context)==ORPHEUS_OK);
    CHECK(!memcmp(impulse,want_impulse,sizeof(impulse)) && !memcmp(reverb,want_reverb,sizeof(reverb)));
    params[0].value.i32=0; CHECK(api->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG);
    params[0].value.i32=65; CHECK(api->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG);
    config.param_count=0; config.state_block=&state; void* allocated=NULL;
    CHECK(api->create(&allocated,&config)==ORPHEUS_OK && allocated==&state && !state.owns_memory);
    CHECK(api->prepare(allocated,&config)==ORPHEUS_OK && api->destroy(allocated)==ORPHEUS_OK);
    config.state_block=NULL;
    CHECK(api->create(&allocated,&config)==ORPHEUS_OK && ((ComplexGainSplitState*)allocated)->owns_memory);
    CHECK(api->destroy(allocated)==ORPHEUS_OK);
    return 0;
}
