#include "component_state.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); return 1; } } while (0)
typedef struct Capture {
    OrpheusBuffer* output;
    unsigned calls;
    int error;
} Capture;
static int emit(void* user, uint32_t port, const OrpheusDelivery* delivery) {
    Capture* capture=user;
    if (port!=1 || !delivery || delivery->buffer!=capture->output) return ORPHEUS_ERR_PROCESSING;
    capture->calls++;
    return capture->error;
}
static int shape(const OrpheusComponentInterface* api, unsigned frames, double rate) {
    size_t count=(size_t)frames*2;
    float* input=malloc((count+1)*sizeof(float)), *output=malloc((count+1)*sizeof(float));
    CHECK(input && output);
    orpheus_builtin_spatial_enhancer_State state;
    OrpheusConfig config={.block_size=frames,.channels=2,.sample_rate=rate,.state_block=&state};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0);
    OrpheusBuffer in={.data=input,.frame_count=frames,.frame_capacity=frames,.channels=2,
        .format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer out=in;out.data=output;
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&out};
    Capture capture={.output=&out};OrpheusContinuation continuation={.emit=emit,.user=&capture};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,
        .frame_count=frames,.continuation=&continuation};
    input[count]=output[count]=123.25f;
    for (size_t i=0;i<count;i++) input[i]=((int)(i%103)-51)*.03125f;
    CHECK(api->process(handle,&ctx)==0 && capture.calls==1 && out.frame_count==frames);
    CHECK(!memcmp(input,output,count*sizeof(float)) && output[count]==123.25f && input[count]==123.25f);
    CHECK(api->reset(handle)==0);
    OrpheusValue value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=4};
    CHECK(api->set_parameter(handle,"width",&value)==0 && state.algorithm.width_smoothed==1);
    value.value.f32=1;CHECK(api->set_parameter(handle,"air",&value)==0 && state.algorithm.air_smoothed==0);
    value.value.f32=.5f;CHECK(api->set_parameter(handle,"mono_mix",&value)==0
        && state.algorithm.mono_mix_smoothed==1);
    float coefficient=rate>0 ? 1.0f-expf(-1.0f/(.005f*(float)rate)) : 1.0f;
    CHECK(state.algorithm.mono_coeff==coefficient);
    float width=1+coefficient*3, air=coefficient, mix=1+coefficient*(-.5f);
    float expected=(1.0f*(1.0f-air)+state.algorithm.hp_gain*air)*width*mix;
    memset(input,0,count*sizeof(float));input[0]=1;input[1]=-1;
    CHECK(api->process(handle,&ctx)==0 && capture.calls==2);
    CHECK(!memcmp(output,&expected,sizeof(float)) && output[1]==-expected && output[count]==123.25f);
    for (unsigned f=0;f<frames;f++) CHECK(isfinite(output[2*f]) && output[2*f]==-output[2*f+1]);
    CHECK(api->reset(handle)==0 && state.algorithm.width_smoothed==4
        && state.algorithm.air_smoothed==1 && state.algorithm.mono_mix_smoothed==.5f
        && state.algorithm.hp_prev_side==0 && state.algorithm.hp_prev_out==0
        && state.algorithm.mono_coeff==coefficient);
    CHECK(api->prepare(handle,&config)==0 && state.width==1 && state.air==0 && state.mono_mix==1);
    capture.calls=0;capture.error=-77;
    input[count-2]=.5f;input[count-1]=-.5f;
    CHECK(api->process(handle,&ctx)==-77 && capture.calls==1);
    CHECK(state.algorithm.hp_prev_side==.5f);
    capture.error=0;CHECK(api->reset(handle)==0);
    input[0]=NAN;input[1]=INFINITY;
    CHECK(api->process(handle,&ctx)==0);
    for (size_t i=0;i<count;i++) CHECK(isnan(output[i]));
    orpheus_builtin_spatial_enhancer_State saved=state;
    capture.calls=0;in.frame_count=frames-1;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !capture.calls);in.frame_count=frames;
    out.frame_capacity=frames-1;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !capture.calls);out.frame_capacity=frames;
    out.data=input;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !capture.calls);
    out.data=input+1;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !capture.calls);
    out.data=input;in.data=input+1;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !capture.calls);in.data=input;out.data=output;
    inputs[0]=NULL;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !capture.calls);inputs[0]=&in;
    outputs[0]=NULL;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !capture.calls);
    ctx.frame_count=0;CHECK(api->process(handle,&ctx)==0 && !capture.calls);
    value.value.f32=-.1f;
    CHECK(api->set_parameter(handle,"width",&value)==ORPHEUS_ERR_INVALID_ARG);
    value.value.f32=NAN;CHECK(api->set_parameter(handle,"air",&value)==ORPHEUS_ERR_INVALID_ARG);
    config.channels=1;CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(!memcmp(&state,&saved,sizeof(state)));
    CHECK(api->reset(handle)==0 && api->destroy(handle)==0);
    config.channels=2;config.state_block=NULL;
    CHECK(api->create(&handle,&config)==ORPHEUS_ERR_INVALID_ARG);
    free(input);free(output);
    return 0;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    CHECK(shape(api,1,48000)==0 && shape(api,17,44100)==0
        && shape(api,65536,48000)==0 && shape(api,7,0)==0);
    return 0;
}
