#include "component_state.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); return 1; } } while (0)
typedef struct Capture {
    const OrpheusBuffer* output;
    orpheus_builtin_switch_State* state;
    float seen;
    unsigned calls;
    int error;
} Capture;
static int emit(void* user, uint32_t port, const OrpheusDelivery* delivery) {
    Capture* capture=user;
    if (port!=1 || !delivery || delivery->buffer!=capture->output) return ORPHEUS_ERR_PROCESSING;
    capture->calls++;capture->seen=capture->state->algorithm.gain;
    return capture->error;
}
static int shape(const OrpheusComponentInterface* api, unsigned channels, unsigned frames,
    float initial, float ramp, double rate) {
    size_t count=(size_t)channels*frames, bytes=count*sizeof(float);
    float* original=malloc(bytes), *input=malloc(bytes+sizeof(float)), *output=malloc(bytes+sizeof(float));
    CHECK(original && input && output);
    const char* ids[]={"channels","enable","ramp_ms"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=channels},
        {.type=ORPHEUS_VALUE_FLOAT,.value.f32=initial},{.type=ORPHEUS_VALUE_FLOAT,.value.f32=ramp}};
    orpheus_builtin_switch_State state;
    OrpheusConfig config={.block_size=frames,.sample_rate=rate,.state_block=&state,
        .param_ids=ids,.param_values=params,.param_count=3};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0);
    float coefficient=ramp<=0 || rate==0 ? 1 : 1.0f-expf(-1.0f/((ramp/1000.0f)*(float)rate));
    float gain=initial>=.5f ? 1 : 0;
    CHECK(state.algorithm.smoothing_coeff==coefficient && state.algorithm.gain==gain);
    OrpheusBuffer in={.data=input,.frame_count=frames,.frame_capacity=frames,.channels=channels,
        .format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer out=in;out.data=output;
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&out};
    Capture capture={.output=&out,.state=&state};
    OrpheusContinuation continuation={.emit=emit,.user=&capture};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,
        .frame_count=frames,.continuation=&continuation};
    for (unsigned pass=0;pass<6;pass++) {
        float target=pass<2 ? (initial>=.5f ? 1 : 0) : pass==2 ? 0 : 1;
        if (pass>=2) {
            OrpheusValue value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=pass==2 ? .499f : .5f};
            CHECK(api->set_parameter(handle,"enable",&value)==0 && state.algorithm.gain==gain);
        }
        for (size_t i=0;i<count;i++) original[i]=((int)((i*13+pass*11)%101)-50)*.03125f;
        if (pass==5) {
            original[0]=INFINITY;
            if (count>1) original[1]=NAN;
        }
        memcpy(input,original,bytes);input[count]=output[count]=123.25f;
        out.data=pass==1 ? input : output;capture.calls=0;capture.error=pass==4 ? -77 : 0;
        CHECK(api->process(handle,&ctx)==capture.error && capture.calls==1 && out.frame_count==frames);
        for (size_t i=0;i<count;i++) {
            gain+=coefficient*(target-gain);
            float expected=original[i]*gain, actual=((float*)out.data)[i];
            CHECK(isnan(expected) ? isnan(actual) : !memcmp(&actual,&expected,sizeof(float)));
        }
        CHECK(state.algorithm.gain==gain && capture.seen==gain);
        CHECK(input[count]==123.25f && output[count]==123.25f);
    }
    orpheus_builtin_switch_State saved=state;
    capture.calls=0;capture.error=0;in.frame_count=frames-1;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !capture.calls);in.frame_count=frames;
    out.frame_capacity=frames-1;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !capture.calls);out.frame_capacity=frames;
    if (count>1) {
        out.data=input+1;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !capture.calls);
        in.data=input+1;out.data=input;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !capture.calls);
        in.data=input;
    }
    out.data=output;inputs[0]=NULL;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !capture.calls);inputs[0]=&in;
    outputs[0]=NULL;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !capture.calls);
    ctx.frame_count=0;CHECK(api->process(handle,&ctx)==0 && !capture.calls);
    OrpheusValue value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=1.1f};
    CHECK(api->set_parameter(handle,"enable",&value)==ORPHEUS_ERR_INVALID_ARG);
    value.value.f32=NAN;CHECK(api->set_parameter(handle,"enable",&value)==ORPHEUS_ERR_INVALID_ARG);
    params[0].value.i32=33;CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(!memcmp(&state,&saved,sizeof(state)));
    CHECK(api->reset(handle)==0 && state.enable==1 && state.algorithm.gain==1
        && state.algorithm.smoothing_coeff==coefficient);
    params[0].value.i32=channels;
    CHECK(api->prepare(handle,&config)==0 && state.enable==initial
        && state.algorithm.gain==(initial>=.5f ? 1 : 0));
    CHECK(api->destroy(handle)==0);
    config.state_block=NULL;CHECK(api->create(&handle,&config)==ORPHEUS_ERR_INVALID_ARG);
    free(original);free(input);free(output);
    return 0;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    CHECK(shape(api,1,1,1,20,48000)==0 && shape(api,3,17,.49f,0,48000)==0
        && shape(api,32,65536,.5f,1000,48000)==0 && shape(api,2,7,0,20,0)==0);
    return 0;
}
