#include "component_state.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); return 1; } } while (0)
static int emit(void* user, uint32_t port, const OrpheusDelivery* delivery) {
    (void)port; (void)delivery;
    (*(unsigned*)user)++;
    return ORPHEUS_ERR_PROCESSING;
}
static uint32_t read_u32(const unsigned char* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static int shape(const OrpheusComponentInterface* api, unsigned channels, unsigned frames) {
    size_t count=(size_t)channels*frames, bytes=count*sizeof(float);
    float* samples=malloc(bytes);
    CHECK(samples);
    for(size_t i=0;i<count;i++) samples[i]=(float)((int)(i%103)-51)*.03125f;
    const char* ids[]={"channels"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=channels}};
    orpheus_builtin_wav_out_State state;
    OrpheusConfig config={.sample_rate=44100,.block_size=frames,.param_count=1,
        .param_ids=ids,.param_values=params,.state_block=&state};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0);
    CHECK(state.sample_rate==0 && state.algorithm.sample_rate==44100);
    OrpheusBuffer in={.data=samples,.frame_count=frames,.frame_capacity=frames,
        .channels=channels,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    const OrpheusBuffer* inputs[]={&in};
    unsigned calls=0;
    OrpheusContinuation continuation={.emit=emit,.user=&calls};
    OrpheusProcessContext ctx={.inputs=inputs,.input_count=1,.frame_count=frames,.continuation=&continuation};
    CHECK(api->process(handle,&ctx)==0 && state.algorithm.count==count && !calls);
    CHECK(!memcmp(samples,state.algorithm.samples,bytes));
    in.frame_count=frames-1;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && state.algorithm.count==count);
    in.frame_count=frames;inputs[0]=NULL;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);inputs[0]=&in;
    samples[0]=NAN;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && state.algorithm.count==count);
    samples[0]=-INFINITY;
    CHECK(api->process(handle,&ctx)==0 && state.algorithm.count==2*count);
    CHECK(api->reset(handle)==0 && state.algorithm.count==0);
    samples[0]=.5f;
    state.algorithm.count=ORPHEUS_WAV_OUT_CAPACITY-(uint32_t)count;
    CHECK(api->process(handle,&ctx)==0 && state.algorithm.count==ORPHEUS_WAV_OUT_CAPACITY);
    CHECK(!memcmp(samples,state.algorithm.samples+ORPHEUS_WAV_OUT_CAPACITY-count,bytes));
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_OUT_OF_MEMORY
        && state.algorithm.count==ORPHEUS_WAV_OUT_CAPACITY);
    ctx.frame_count=0;inputs[0]=NULL;
    CHECK(api->process(handle,&ctx)==0 && !calls);
    CHECK(api->reset(handle)==0);
    CHECK(api->destroy(handle)==0 && !state.algorithm.samples);
    free(samples);
    return 0;
}

static int file_lifecycle(const OrpheusComponentInterface* api) {
    const char* path="wav_out_smoke.wav";
    remove(path);
    const char* ids[]={"file_path","channels","sample_rate"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_STRING,.value.str=path},
        {.type=ORPHEUS_VALUE_INT,.value.i32=1},{.type=ORPHEUS_VALUE_INT,.value.i32=32000}};
    orpheus_builtin_wav_out_State state;
    OrpheusConfig config={.sample_rate=48000,.block_size=8,.state_block=&state,
        .param_ids=ids,.param_values=params,.param_count=3};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0);
    CHECK(state.sample_rate==32000 && state.algorithm.sample_rate==32000);
    float values[]={-INFINITY,-1,-.5f,-0.0f,.5f,1,INFINITY,2};
    OrpheusBuffer in={.data=values,.frame_count=8,.frame_capacity=8,.channels=1,
        .format=ORPHEUS_FORMAT_F32,.interleaved=true};
    const OrpheusBuffer* inputs[]={&in};
    OrpheusProcessContext ctx={.inputs=inputs,.input_count=1,.frame_count=8};
    CHECK(api->process(handle,&ctx)==0);
    FILE* file=fopen(path,"rb");CHECK(!file);
    char too_long[513];memset(too_long,'x',sizeof(too_long)-1);too_long[512]=0;
    params[0].value.str=too_long;
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && state.algorithm.count==8);
    params[0].value.str=path;
    params[1].value.i32=65;
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && state.algorithm.count==8);
    params[1].value.i32=1;params[2].value.i32=-1;
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && state.algorithm.count==8);
    params[2].value.i32=32000;
    CHECK(api->reset(handle)==0 && state.algorithm.count==0);
    CHECK(api->process(handle,&ctx)==0 && api->destroy(handle)==0);
    file=fopen(path,"rb");CHECK(file);
    unsigned char data[60];
    CHECK(fread(data,1,sizeof(data),file)==sizeof(data) && fgetc(file)==EOF && fclose(file)==0);
    CHECK(!memcmp(data,"RIFF",4) && !memcmp(data+8,"WAVEfmt ",8));
    CHECK(read_u32(data+24)==32000 && read_u32(data+28)==64000 && read_u32(data+40)==16);
    for(unsigned i=0;i<8;i++) {
        float value=values[i]>1 ? 1 : values[i]<-1 ? -1 : values[i];
        uint16_t expected=(uint16_t)(int16_t)(value*32767.0f);
        CHECK(data[44+2*i]==(uint8_t)expected && data[45+2*i]==(uint8_t)(expected>>8));
    }
    CHECK(remove(path)==0);
    params[0].value.str="wav_out_missing_parent/child/out.wav";
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0);
    CHECK(api->destroy(handle)==ORPHEUS_ERR_NOT_FOUND && !state.algorithm.samples);
    params[0].value.str="";
    params[2].value.i32=0;config.sample_rate=0;
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0);
    CHECK(state.algorithm.sample_rate==48000 && api->destroy(handle)==0);
    config.sample_rate=48000;params[1].value.i32=64;params[2].value.i32=2147483647;
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(!state.algorithm.samples && api->destroy(handle)==0);
    return 0;
}

int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    CHECK(api->get_descriptor()->abi_version==7);
    CHECK(shape(api,1,1)==0 && shape(api,2,257)==0 && shape(api,64,65536)==0);
    CHECK(file_lifecycle(api)==0);
    return 0;
}
