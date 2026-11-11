#include "component_state.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); return 1; } } while (0)
typedef struct Capture {
    OrpheusBuffer* output;
    orpheus_builtin_mixer_State* state;
    float seen[2];
    unsigned calls;
    int error;
} Capture;
static int emit(void* user, uint32_t port, const OrpheusDelivery* delivery) {
    Capture* capture=user;
    if (port!=1 || !delivery || delivery->buffer!=capture->output) return ORPHEUS_ERR_PROCESSING;
    memcpy(capture->seen,capture->state->algorithm.gain_linear,sizeof(capture->seen));
    capture->calls++;
    return capture->error;
}
static int shape(const OrpheusComponentInterface* api, unsigned channels, unsigned frames) {
    size_t count=(size_t)channels*frames,bytes=count*sizeof(float);
    float* a=malloc(bytes+sizeof(float)), *b=malloc(bytes+sizeof(float)), *output=malloc(bytes+sizeof(float));
    CHECK(a && b && output);
    const char* ids[]={"channels","gain0","gain1"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=channels},
        {.type=ORPHEUS_VALUE_FLOAT,.value.f32=-96},{.type=ORPHEUS_VALUE_FLOAT,.value.f32=24}};
    orpheus_builtin_mixer_State state;
    OrpheusConfig config={.block_size=frames,.param_ids=ids,.param_values=params,
        .param_count=3,.state_block=&state};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0);
    OrpheusBuffer in0={.data=a,.channels=channels,.frame_count=frames,.frame_capacity=frames,
        .format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer in1=in0,out=in0;in1.data=b;out.data=output;
    const OrpheusBuffer* inputs[]={&in0,&in1};OrpheusBuffer* outputs[]={&out};
    Capture capture={.output=&out,.state=&state};
    OrpheusContinuation continuation={.emit=emit,.user=&capture};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=2,.output_count=1,
        .frame_count=frames,.continuation=&continuation};
    float gain[]={1,1},db[]={-96,24};
    CHECK(state.algorithm.gain_linear[0]==1 && state.algorithm.gain_linear[1]==1);
    for (unsigned pass=0;pass<6;pass++) {
        if (pass==2) {
            db[0]=-20;db[1]=20;
            OrpheusValue value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=db[0]};
            CHECK(api->set_parameter(handle,"gain0",&value)==0);
            value.value.f32=db[1];CHECK(api->set_parameter(handle,"gain1",&value)==0);
            CHECK(!memcmp(gain,state.algorithm.gain_linear,sizeof(gain)));
        }
        float target[]={powf(10,db[0]/20),powf(10,db[1]/20)};
        for (size_t i=0;i<count;i++) {
            a[i]=((int)((i*13+pass*11)%101)-50)*.03125f;
            b[i]=((int)((i*19+pass*7)%97)-48)*.0625f;
        }
        if (pass==5) {
            a[0]=INFINITY;b[0]=-INFINITY;
            if (count>1) a[1]=NAN;
        }
        if (pass==1) in1.data=a;
        else in1.data=b;
        capture.calls=0;capture.error=pass==4 ? -77 : 0;ctx.trigger_input=pass%2;
        a[count]=b[count]=output[count]=123.25f;
        CHECK(api->process(handle,&ctx)==capture.error && capture.calls==1 && out.frame_count==frames);
        for (size_t i=0;i<count;i++) {
            gain[0]+=target[0]-gain[0];gain[1]+=target[1]-gain[1];
            float expected=a[i]*gain[0]+((float*)in1.data)[i]*gain[1];
            CHECK(isnan(expected) ? isnan(output[i]) : !memcmp(output+i,&expected,sizeof(float)));
        }
        CHECK(!memcmp(gain,state.algorithm.gain_linear,sizeof(gain))
            && !memcmp(gain,capture.seen,sizeof(gain)));
        CHECK(a[count]==123.25f && b[count]==123.25f && output[count]==123.25f);
    }
    orpheus_builtin_mixer_State saved=state;
    capture.calls=0;capture.error=0;
    for (unsigned port=0;port<2;port++) {
        OrpheusBuffer* in=port ? &in1 : &in0;
        inputs[port]=NULL;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !capture.calls);
        inputs[port]=in;in->frame_count=frames-1;
        CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !capture.calls);in->frame_count=frames;
        out.data=in->data;
        CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !capture.calls);
        if (count>1) {
            out.data=(float*)in->data+1;
            CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !capture.calls);
            out.data=in->data;in->data=(float*)in->data+1;
            CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !capture.calls);
            in->data=(float*)in->data-1;
        }
        out.data=output;
    }
    out.frame_capacity=frames-1;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !capture.calls);out.frame_capacity=frames;
    outputs[0]=NULL;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !capture.calls);
    ctx.frame_count=0;CHECK(api->process(handle,&ctx)==0 && !capture.calls);
    OrpheusValue value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=-97};
    CHECK(api->set_parameter(handle,"gain0",&value)==ORPHEUS_ERR_INVALID_ARG);
    value.value.f32=INFINITY;CHECK(api->set_parameter(handle,"gain1",&value)==ORPHEUS_ERR_INVALID_ARG);
    params[0].value.i32=33;CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(!memcmp(&state,&saved,sizeof(state)));
    OrpheusValue readback;
    CHECK(api->get_parameter(handle,"gain0",&readback)==0 && readback.value.f32==-20);
    CHECK(api->reset(handle)==0 && state.gain0==0 && state.gain1==0);
    for (unsigned i=0;i<2;i++) CHECK(state.algorithm.gain_linear[i]==1 && state.algorithm.target_gain_linear[i]==1);
    params[0].value.i32=channels;
    CHECK(api->prepare(handle,&config)==0 && state.gain0==-96 && state.gain1==24);
    CHECK(api->destroy(handle)==0);
    config.state_block=NULL;CHECK(api->create(&handle,&config)==ORPHEUS_ERR_INVALID_ARG);
    free(a);free(b);free(output);
    return 0;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    CHECK(shape(api,1,1)==0 && shape(api,3,17)==0 && shape(api,32,65536)==0);
    return 0;
}
