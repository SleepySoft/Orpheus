#include "component_state.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); return 1; } } while(0)

static void u16(uint8_t* p,uint16_t value) { p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8); }
static void u32(uint8_t* p,uint32_t value) {
    for(unsigned n=0;n<4;n++) p[n]=(uint8_t)(value>>(8*n));
}
static int write_file(const uint8_t* header,size_t length) {
    const uint8_t data[]={0,128,255,127,0,64,0,192,0,0,1,0};
    FILE* file=fopen("wav_in_smoke.wav","wb");
    if(!file) return 1;
    int rc=fwrite(header,1,44,file)!=44 || fwrite(data,1,length,file)!=length;
    if(fclose(file)) rc=1;
    return rc;
}
typedef struct { unsigned calls; } Capture;
static bool tick_fail;
static int ordered_delivery(void* user,uint32_t port,const OrpheusDelivery* delivery) {
    Capture* capture=(Capture*)user;
    if(!delivery || delivery->epoch!=4 || delivery->frame_index!=93 ||
       port!=capture->calls+1 || (port==1 ? !delivery->buffer : delivery->buffer!=NULL))
        return ORPHEUS_ERR_INVALID_ARG;
    ++capture->calls;
    return port==2 && tick_fail ? -88 : ORPHEUS_OK;
}
static int fail_delivery(void* user,uint32_t port,const OrpheusDelivery* delivery) {
    Capture* capture=(Capture*)user;
    capture->calls++;
    if(port!=1 || !delivery || !delivery->buffer || delivery->buffer->frame_count!=2 ||
       delivery->epoch!=4 || delivery->frame_index!=93) return ORPHEUS_ERR_INVALID_ARG;
    return ORPHEUS_ERR_PROCESSING;
}
int main(void) {
    uint8_t header[44]={0},original[44];
    memcpy(header,"RIFF",4);u32(header+4,48);memcpy(header+8,"WAVEfmt ",8);u32(header+16,16);
    u16(header+20,1);u16(header+22,2);u32(header+24,48000);u32(header+28,192000);
    u16(header+32,4);u16(header+34,16);memcpy(header+36,"data",4);u32(header+40,12);
    memcpy(original,header,44);
    CHECK(!write_file(header,12));
    const OrpheusComponentInterface* api=orpheus_get_interface();
    orpheus_builtin_wav_in_State state={0};
    const char* ids[]={"file_path","channels"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_STRING,.value.str="wav_in_smoke.wav"},
        {.type=ORPHEUS_VALUE_INT,.value.i32=3}};
    OrpheusConfig config={.sample_rate=48000,.block_size=2,.state_block=&state,
        .param_count=2,.param_ids=ids,.param_values=params};
    void* instance=NULL;
    CHECK(api->create(&instance,&config)==ORPHEUS_OK && instance==&state);
    CHECK(api->prepare(&state,&config)==ORPHEUS_OK && state.algorithm.total_frames==3);
    float output[6];
    OrpheusBuffer buffer={.data=output,.channels=3,.frame_capacity=2,.format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer* outputs[]={&buffer,NULL};
    OrpheusProcessContext ctx={.outputs=outputs,.output_count=2,.frame_count=2,.valid_frames=2,.epoch=4,.frame_index=93};
    CHECK(api->process(&state,&ctx)==ORPHEUS_OK && state.algorithm.position==2);
    const float expected[]={-1,32767.0f/32768,-1,.5f,-.5f,.5f};
    CHECK(!memcmp(output,expected,sizeof(expected)));
    CHECK(api->process(&state,&ctx)==ORPHEUS_OK && state.algorithm.position==3 &&
        output[0]==0 && output[1]==1.0f/32768 && output[2]==0);
    for(unsigned n=3;n<6;n++) CHECK(output[n]==0);
    CHECK(api->reset(&state)==ORPHEUS_OK && state.algorithm.position==0 && state.algorithm.total_frames==3);
    ctx.frame_count=0;
    CHECK(api->process(&state,&ctx)==ORPHEUS_OK && !state.algorithm.position && !buffer.frame_count);
    ctx.frame_count=2;
    Capture capture={0};
    OrpheusContinuation continuation={.user=&capture,.emit=fail_delivery};
    ctx.continuation=&continuation;
    continuation.emit=ordered_delivery;
    CHECK(api->process(&state,&ctx)==ORPHEUS_OK && capture.calls==2 && state.algorithm.position==2);
    CHECK(api->reset(&state)==ORPHEUS_OK);
    capture.calls=0;tick_fail=true;
    CHECK(api->process(&state,&ctx)==-88 && capture.calls==2 && state.algorithm.position==2);
    CHECK(api->reset(&state)==ORPHEUS_OK);
    capture.calls=0;continuation.emit=fail_delivery;
    ctx.continuation=&continuation;
    CHECK(api->process(&state,&ctx)==ORPHEUS_ERR_PROCESSING && capture.calls==1 && state.algorithm.position==2);
    ctx.continuation=NULL;buffer.data=state.algorithm.samples;
    CHECK(api->process(&state,&ctx)==ORPHEUS_ERR_INVALID_ARG && state.algorithm.position==2);
    buffer.data=output;
    for(unsigned fault=0;fault<8;fault++) {
        memcpy(header,original,44);
        switch(fault) {
            case 0:u16(header+22,0);break;
            case 1:u16(header+34,0);break;
            case 2:u16(header+20,6);break;
            case 3:u16(header+34,24);break;
            case 4:u16(header+32,3);break;
            case 5:u32(header+40,11);break;
            case 6:u32(header+4,49);break;
            default:memcpy(header+36,"JUNK",4);break;
        }
        CHECK(!write_file(header,12));
        CHECK(api->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG && !state.algorithm.samples);
    }
    CHECK(!write_file(original,11));
    CHECK(api->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG);
    memcpy(header,original,44);u32(header+4,36);u32(header+40,0);
    CHECK(!write_file(header,0));
    CHECK(api->prepare(&state,&config)==ORPHEUS_OK && !state.algorithm.total_frames);
    CHECK(api->process(&state,&ctx)==ORPHEUS_OK);
    for(unsigned n=0;n<6;n++) CHECK(output[n]==0);
    params[0].value.str="__missing_wav_in_smoke__.wav";
    CHECK(api->prepare(&state,&config)==ORPHEUS_ERR_NOT_FOUND);
    params[0].value.str="";
    CHECK(api->prepare(&state,&config)==ORPHEUS_OK);
    CHECK(api->destroy(&state)==ORPHEUS_OK);
    CHECK(!remove("wav_in_smoke.wav"));
    return 0;
}
