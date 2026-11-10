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
static int maximum_shapes(const OrpheusComponentInterface* api) {
    const char* ids[]={"fft_size","channels","batches","first_bin","bins"};
    for(unsigned shape=0;shape<2;shape++) {
        unsigned fft=shape ? 2 : 65536,channels=shape ? 64 : 1,batches=64,bins=fft/2+1;
        size_t count=(size_t)fft*batches*channels,out_count=(size_t)bins*2*batches*channels;
        uint32_t* input=malloc(count*4);uint32_t* output=malloc(out_count*4);
        CHECK(input && output);
        for(size_t i=0;i<count;i++) input[i]=0x3f000000u|((uint32_t)i*2654435761u&0x007fffffu);
        OrpheusValue values[]={
            {.type=ORPHEUS_VALUE_INT,.value.i32=fft},{.type=ORPHEUS_VALUE_INT,.value.i32=channels},
            {.type=ORPHEUS_VALUE_INT,.value.i32=batches},{.type=ORPHEUS_VALUE_INT,.value.i32=0},
            {.type=ORPHEUS_VALUE_INT,.value.i32=bins}};
        orpheus_builtin_halfcomplex_decode_State state;void* handle=NULL;
        OrpheusConfig config={.sample_rate=48000,.block_size=8,.state_block=&state,
            .param_ids=ids,.param_values=values,.param_count=5};
        OrpheusBuffer in={.data=input,.channels=channels,.frame_count=fft*batches,.frame_capacity=fft*batches,
            .format=ORPHEUS_FORMAT_F32,.interleaved=true};
        OrpheusBuffer out={.data=output,.channels=channels,.frame_capacity=bins*2*batches,
            .format=ORPHEUS_FORMAT_F32,.interleaved=true};
        const OrpheusBuffer* inputs[]={&in,NULL};OrpheusBuffer* outputs[]={&out,NULL};
        OrpheusContinuation continuation={.emit=emit};
        OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=2,.output_count=2,
            .frame_count=8,.continuation=&continuation};
        CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0);
        for(unsigned typed=0;typed<2;typed++) {
            inputs[0]=typed ? NULL : &in;inputs[1]=typed ? &in : NULL;
            calls=0;
            CHECK(api->process(handle,&ctx)==0 && calls==2 && ports[0]==1 && ports[1]==2);
            CHECK(out.frame_count==bins*2*batches);
            for(unsigned batch=0;batch<batches;batch++) for(unsigned bin=0;bin<bins;bin++)
                for(unsigned channel=0;channel<channels;channel++) {
                    size_t base=((size_t)batch*bins*2+bin*2)*channels+channel;
                    CHECK(output[base]==input[((size_t)batch*fft+bin)*channels+channel]);
                    CHECK(output[base+channels]==(bin==0 || bin==fft/2 ? 0
                        : input[((size_t)batch*fft+fft-bin)*channels+channel]));
                }
        }
        values[0].value.i32=65537;
        CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG);
        CHECK(api->destroy(handle)==0);
        free(input);free(output);
    }
    return 0;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    CHECK(maximum_shapes(api)==0);
    const char* ids[]={"fft_size","channels","batches","first_bin","bins"};
    OrpheusValue values[5];
    for (unsigned i=0;i<5;++i) values[i].type=ORPHEUS_VALUE_INT;
    uint32_t input[144],output[162],expected[162];
    const uint32_t patterns[]={0,0x80000000u,0x7f800000u,0xff800000u,0x7fc12345u,0x7f812345u,0x3f123456u};
    for (unsigned i=0;i<144;++i) input[i]=patterns[i%7];
    OrpheusBuffer in={.data=input,.channels=3,.frame_capacity=48,.frame_count=48,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer out={.data=output,.channels=3,.frame_capacity=54,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    const OrpheusBuffer* inputs[]={&in,NULL}; OrpheusBuffer* outputs[]={&out,NULL};
    OrpheusContinuation continuation={.emit=emit};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=2,.output_count=2,.frame_count=8,
        .continuation=&continuation};
    OrpheusConfig config={.sample_rate=48000,.block_size=48,.param_count=5,.param_ids=ids,.param_values=values};
    for (unsigned selected=0;selected<2;++selected) {
        unsigned first=selected ? 2 : 0,bins=selected ? 3 : 9;
        values[0].value.i32=16; values[1].value.i32=3; values[2].value.i32=3;
        values[3].value.i32=first; values[4].value.i32=bins;
        orpheus_builtin_halfcomplex_decode_State state;void* handle=NULL;
        config.state_block=&state;CHECK(api->create(&handle,&config)==0 && handle==&state);
        CHECK(api->prepare(&state,&config)==ORPHEUS_OK);
        for (unsigned h=0;h<3;++h) for (unsigned k=0;k<bins;++k) for (unsigned c=0;c<3;++c) {
            unsigned bin=first+k,pos=(h*bins*2+k*2)*3+c;
            expected[pos]=input[(h*16+bin)*3+c];
            expected[pos+3]=bin==0 || bin==8 ? 0 : input[(h*16+16-bin)*3+c];
        }
        for (unsigned typed=0;typed<2;++typed) {
            inputs[0]=typed ? NULL : &in; inputs[1]=typed ? &in : NULL;
            calls=0;
            CHECK(api->process(&state,&ctx)==ORPHEUS_OK && out.frame_count==bins*2*3);
            CHECK(calls==2 && ports[0]==1 && ports[1]==2);
            CHECK(!memcmp(output,expected,bins*2*3*3*4));
        }
        uint32_t before[162]; memcpy(before,output,sizeof(output));
        inputs[0]=&in;
        CHECK(api->process(&state,&ctx)==ORPHEUS_ERR_INVALID_ARG && !memcmp(before,output,sizeof(output)));
        inputs[1]=NULL; out.frame_capacity=1;
        CHECK(api->process(&state,&ctx)==ORPHEUS_ERR_INVALID_ARG && !memcmp(before,output,sizeof(output)));
        out.frame_capacity=54; in.interleaved=false;
        CHECK(api->process(&state,&ctx)==ORPHEUS_ERR_INVALID_ARG && !memcmp(before,output,sizeof(output)));
        in.interleaved=true; in.frame_count=47;
        CHECK(api->process(&state,&ctx)==ORPHEUS_ERR_INVALID_ARG && !memcmp(before,output,sizeof(output)));
        in.frame_count=48; in.frame_capacity=47;
        CHECK(api->process(&state,&ctx)==ORPHEUS_ERR_INVALID_ARG && !memcmp(before,output,sizeof(output)));
        in.frame_capacity=48; in.channels=2;
        CHECK(api->process(&state,&ctx)==ORPHEUS_ERR_INVALID_ARG && !memcmp(before,output,sizeof(output)));
        in.channels=3; out.data=NULL;
        CHECK(api->process(&state,&ctx)==ORPHEUS_ERR_INVALID_ARG);
        out.data=input;
        CHECK(api->process(&state,&ctx)==ORPHEUS_ERR_INVALID_ARG);
        out.data=output; ctx.frame_count=0;
        CHECK(api->process(&state,&ctx)==ORPHEUS_OK && !memcmp(before,output,sizeof(output)));
        ctx.frame_count=8;
        CHECK(api->reset(&state)==ORPHEUS_OK);
        values[3].value.i32=9;
        CHECK(api->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG);
        fail_port=1;calls=0;CHECK(api->process(&state,&ctx)==-77 && calls==1);
        fail_port=2;calls=0;CHECK(api->process(&state,&ctx)==-77 && calls==2);fail_port=0;
        CHECK(api->destroy(handle)==0);
    }
    for (unsigned n=2;n<=6;n+=4) {
        orpheus_builtin_halfcomplex_decode_State state;void* handle=NULL;
        values[0].value.i32=n; values[1].value.i32=1; values[2].value.i32=1;
        values[3].value.i32=0; values[4].value.i32=n/2+1;
        config.state_block=&state;CHECK(api->create(&handle,&config)==0);
        CHECK(api->prepare(&state,&config)==ORPHEUS_OK);
        in.channels=out.channels=1; in.frame_count=n; ctx.frame_count=8;
        CHECK(api->process(&state,&ctx)==ORPHEUS_OK && out.frame_count==n+2);
        for (unsigned k=0;k<=n/2;++k) {
            CHECK(output[k*2]==input[k]);
            CHECK(output[k*2+1]==(k==0 || k==n/2 ? 0 : input[n-k]));
        }
        outputs[0]=NULL; CHECK(api->process(&state,&ctx)==ORPHEUS_OK); outputs[0]=&out;
        inputs[0]=NULL; CHECK(api->process(&state,&ctx)==ORPHEUS_ERR_INVALID_ARG); inputs[0]=&in;
        values[0].value.i32=3; CHECK(api->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG);
        values[0].value.i32=65537; CHECK(api->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG);
        CHECK(api->destroy(handle)==0);
    }
    orpheus_builtin_halfcomplex_decode_State external;
    {
        orpheus_builtin_halfcomplex_decode_State decoded;void* handle=NULL;
        config.param_count=0;config.state_block=&decoded;
        CHECK(api->create(&handle,&config)==0 && api->prepare(&decoded,&config)==ORPHEUS_OK);
        uint32_t packed[512]={0},unpacked[516],saved[516];int32_t flag=73;
        OrpheusBuffer source={.data=packed,.channels=2,.frame_count=256,.frame_capacity=256,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
        OrpheusBuffer result={.data=unpacked,.channels=2,.frame_count=7,.frame_capacity=258,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
        OrpheusBuffer tick={.data=&flag,.channels=1,.frame_capacity=1,.format=ORPHEUS_FORMAT_I32,.interleaved=true};
        const OrpheusBuffer* sources[]={&source,NULL};OrpheusBuffer* targets[]={&result,&tick};
        OrpheusProcessContext call={.inputs=sources,.outputs=targets,.input_count=2,.output_count=2,.frame_count=128};
        memset(unpacked,0x55,sizeof(unpacked));memcpy(saved,unpacked,sizeof(saved));
        call.frame_count=0;
        CHECK(api->process(&decoded,&call)==ORPHEUS_OK && flag==73 && !memcmp(saved,unpacked,sizeof(saved)));
        call.frame_count=128;
        for (unsigned fault=0;fault<8;++fault) {
            tick.format=ORPHEUS_FORMAT_I32;tick.channels=1;tick.frame_capacity=1;tick.interleaved=true;tick.data=&flag;result.frame_capacity=258;
            if (fault==0) tick.format=ORPHEUS_FORMAT_F32;
            if (fault==1) tick.channels=2;
            if (fault==2) tick.frame_capacity=0;
            if (fault==3) tick.interleaved=false;
            if (fault==4) tick.data=packed+1;
            if (fault==5) tick.data=unpacked+515;
            if (fault==6) result.frame_capacity=0;
            if (fault==7) tick.data=NULL;
            CHECK(api->process(&decoded,&call)==ORPHEUS_ERR_INVALID_ARG && flag==73 && !memcmp(saved,unpacked,sizeof(saved)));
        }
        tick.data=&flag;result.frame_capacity=258;
        CHECK(api->process(&decoded,&call)==ORPHEUS_OK && flag==1 && tick.frame_count==1 && result.frame_count==258);
        targets[0]=NULL;flag=73;
        CHECK(api->process(&decoded,&call)==ORPHEUS_OK && flag==1);
        targets[1]=NULL;
        CHECK(api->process(&decoded,&call)==ORPHEUS_OK);
        CHECK(api->destroy(handle)==0);
    }
    memset(&external,0xff,sizeof(external));
    config.state_block=&external; config.param_count=0;
    void* state=NULL;
    CHECK(api->create(&state,&config)==ORPHEUS_OK && state==&external);
    CHECK(api->prepare(state,&config)==ORPHEUS_OK && api->destroy(state)==ORPHEUS_OK);
    config.state_block=NULL;
    CHECK(api->create(&state,&config)==ORPHEUS_ERR_INVALID_ARG);
    return 0;
}
