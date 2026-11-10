#include "component_state.h"
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
    orpheus_builtin_halfcomplex_encode_State state;
    const char* keys[]={"channels","fft_size","batches","bins"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=1},
        {.type=ORPHEUS_VALUE_INT,.value.i32=65536},{.type=ORPHEUS_VALUE_INT,.value.i32=64},
        {.type=ORPHEUS_VALUE_INT,.value.i32=32769}};
    OrpheusConfig config={.state_block=&state,.param_ids=keys,.param_values=params,.param_count=4};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0);
    unsigned frames=65536*64, input_frames=32769*2*64;
    uint32_t* input=(uint32_t*)malloc((size_t)input_frames*4);
    uint32_t* audio=(uint32_t*)malloc((size_t)frames*4);
    uint32_t* spectrum=(uint32_t*)malloc((size_t)frames*4);
    CHECK(input && audio && spectrum);
    for(unsigned i=0;i<input_frames;i++) input[i]=i^0x7f812345u;
    OrpheusBuffer in={.data=input,.channels=1,.frame_count=input_frames,.frame_capacity=input_frames,
        .format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer a={.data=audio,.channels=1,.frame_capacity=frames,
        .format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer b=a;b.data=spectrum;
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&a,&b};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=2,.frame_count=8};
    CHECK(api->process(handle,&ctx)==0 && a.frame_count==frames && b.frame_count==frames);
    for(unsigned h=0;h<64;h++) for(unsigned k=0;k<32769;k++) {
        unsigned source=h*32769*2+k*2, index=h*65536+k;
        CHECK(audio[index]==input[source] && spectrum[index]==input[source]);
        if(k && k<32768) {
            index=h*65536+65536-k;
            CHECK(audio[index]==input[source+1] && spectrum[index]==input[source+1]);
        }
    }
    params[1].value.i32=65535;
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && state.fft_size==65536);
    free(input);free(audio);free(spectrum);
    CHECK(api->destroy(handle)==0);
    return 0;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    const char* ids[]={"fft_size","channels","batches","bins"};
    OrpheusValue params[4];
    for (unsigned i=0;i<4;++i) params[i].type=ORPHEUS_VALUE_INT;
    uint32_t input[162],audio[144],spectrum[144],expected[144];
    const uint32_t patterns[]={0,0x80000000u,0x7f800000u,0xff800000u,0x7fc12345u,0x7f812345u,0x3f123456u};
    for (unsigned i=0;i<162;++i) input[i]=patterns[i%7];
    OrpheusBuffer in={.data=input,.channels=3,.frame_capacity=54,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer out={.data=audio,.channels=3,.frame_capacity=48,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer bulk={.data=spectrum,.channels=3,.frame_capacity=48,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    const OrpheusBuffer* inputs[]={&in}; OrpheusBuffer* outputs[]={&out,&bulk};
    OrpheusContinuation continuation={.emit=emit};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=2,.frame_count=1,
        .continuation=&continuation};
    OrpheusConfig config={.sample_rate=48000,.block_size=1,.param_count=4,.param_ids=ids,.param_values=params};
    for (unsigned n=2;n<=16;n=n==2 ? 6 : 16) {
        orpheus_builtin_halfcomplex_encode_State state;
        unsigned bins=n/2+1;
        params[0].value.i32=n; params[1].value.i32=3; params[2].value.i32=3; params[3].value.i32=bins;
        config.state_block=&state;void* handle=NULL;
        CHECK(api->create(&handle,&config)==ORPHEUS_OK && handle==&state);
        CHECK(api->prepare(&state,&config)==ORPHEUS_OK);
        in.frame_count=bins*2*3;
        for (unsigned h=0;h<3;++h) for (unsigned k=0;k<bins;++k) for (unsigned c=0;c<3;++c) {
            unsigned source=(h*bins*2+k*2)*3+c;
            expected[(h*n+k)*3+c]=input[source];
            if (k && k<n/2) expected[(h*n+n-k)*3+c]=input[source+3];
        }
        calls=0;
        CHECK(api->process(&state,&ctx)==ORPHEUS_OK && out.frame_count==n*3 && bulk.frame_count==n*3);
        CHECK(calls==2 && ports[0]==1 && ports[1]==2);
        CHECK(!memcmp(audio,expected,n*3*3*4) && !memcmp(spectrum,expected,n*3*3*4));
        uint32_t before[144]; memcpy(before,audio,n*3*3*4);
        bulk.frame_capacity=1;
        CHECK(api->process(&state,&ctx)==ORPHEUS_ERR_INVALID_ARG && !memcmp(audio,before,n*3*3*4));
        bulk.frame_capacity=48; bulk.data=audio;
        CHECK(api->process(&state,&ctx)==ORPHEUS_ERR_INVALID_ARG);
        bulk.data=input; CHECK(api->process(&state,&ctx)==ORPHEUS_ERR_INVALID_ARG);
        bulk.data=spectrum; in.interleaved=false;
        CHECK(api->process(&state,&ctx)==ORPHEUS_ERR_INVALID_ARG);
        in.interleaved=true; --in.frame_count;
        CHECK(api->process(&state,&ctx)==ORPHEUS_ERR_INVALID_ARG); ++in.frame_count;
        out.data=NULL; CHECK(api->process(&state,&ctx)==ORPHEUS_ERR_INVALID_ARG); out.data=audio;
        outputs[0]=NULL; CHECK(api->process(&state,&ctx)==ORPHEUS_OK); outputs[0]=&out;
        outputs[1]=NULL; CHECK(api->process(&state,&ctx)==ORPHEUS_OK); outputs[1]=&bulk;
        ctx.frame_count=0; CHECK(api->process(&state,&ctx)==ORPHEUS_OK); ctx.frame_count=1;
        CHECK(api->reset(&state)==ORPHEUS_OK);
        fail_port=1;calls=0;CHECK(api->process(&state,&ctx)==-77 && calls==1);
        fail_port=2;calls=0;CHECK(api->process(&state,&ctx)==-77 && calls==2);fail_port=0;
        ++params[3].value.i32; CHECK(api->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG);
        CHECK(api->destroy(handle)==ORPHEUS_OK);
        if (n==16) break;
    }
    orpheus_builtin_halfcomplex_encode_State external;
    memset(&external,0xff,sizeof(external));
    config.state_block=&external; config.param_count=0;
    void* state=NULL;
    CHECK(api->create(&state,&config)==ORPHEUS_OK && state==&external);
    CHECK(api->prepare(state,&config)==ORPHEUS_OK && api->destroy(state)==ORPHEUS_OK);
    config.state_block=NULL;
    CHECK(api->create(&state,&config)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(maximum_payload(api)==0);
    return 0;
}
