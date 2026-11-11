#include "component_state.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); return 1; } } while (0)
typedef struct Capture {
    OrpheusBuffer* output;
    orpheus_builtin_noise_slew_State* state;
    float seen;
    unsigned calls;
    int error;
} Capture;
static int emit(void* user, uint32_t port, const OrpheusDelivery* delivery) {
    Capture* capture=user;
    if (port != 1 || !delivery || delivery->buffer != capture->output) return ORPHEUS_ERR_INVALID_ARG;
    capture->calls++; capture->seen=capture->state->algorithm.prev[0];
    return capture->error;
}
static int shape(const OrpheusComponentInterface* api, unsigned channels, unsigned frames, double sample_rate) {
    size_t count=(size_t)channels*frames;
    float* original=malloc(count*sizeof(float)); float* input=malloc((count+1)*sizeof(float));
    float* output=malloc(count*sizeof(float)); CHECK(original && input && output);
    const char* ids[]={"channels", "rise_rate", "fall_rate"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT, .value.i32=channels},
        {.type=ORPHEUS_VALUE_FLOAT, .value.f32=1}, {.type=ORPHEUS_VALUE_FLOAT, .value.f32=2}};
    orpheus_builtin_noise_slew_State state;
    OrpheusConfig config={.sample_rate=sample_rate, .block_size=frames, .state_block=&state,
        .param_ids=ids, .param_values=params, .param_count=3};
    void* handle=NULL;
    CHECK(api->create(&handle,&config) == 0 && api->prepare(handle,&config) == 0);
    OrpheusBuffer in={.data=input, .channels=channels, .frame_count=frames, .frame_capacity=frames,
        .format=ORPHEUS_FORMAT_F32, .interleaved=true};
    OrpheusBuffer out=in; out.data=output;
    const OrpheusBuffer* inputs[]={&in}; OrpheusBuffer* outputs[]={&out};
    Capture capture={.output=&out, .state=&state};
    OrpheusContinuation continuation={.emit=emit, .user=&capture};
    OrpheusProcessContext ctx={.inputs=inputs, .outputs=outputs, .input_count=1, .output_count=1,
        .frame_count=frames, .continuation=&continuation};
    float history[32]={0};
    for (unsigned pass=0; pass<6; pass++) {
        if (pass == 2) {
            OrpheusValue rate={.type=ORPHEUS_VALUE_FLOAT, .value.f32=4};
            CHECK(api->set_parameter(handle,"rise_rate",&rate) == 0);
            rate.value.f32=8; CHECK(api->set_parameter(handle,"fall_rate",&rate) == 0);
            CHECK(!memcmp(history,state.algorithm.prev,sizeof(history)));
            CHECK(state.algorithm.rise_delta == (sample_rate > 0 ? 4.0f/(float)sample_rate : .0001f));
            CHECK(state.algorithm.fall_delta == (sample_rate > 0 ? 8.0f/(float)sample_rate : .0001f));
        }
        float rise=sample_rate > 0 ? (pass<2 ? 1.0f : 4.0f)/(float)sample_rate : .0001f;
        float fall=sample_rate > 0 ? (pass<2 ? 2.0f : 8.0f)/(float)sample_rate : .0001f;
        for (size_t i=0; i<count; i++) original[i]=((int)((i*13+pass*11)%101)-50)*.03125f;
        if (pass == 5) {
            for (unsigned c=0; c<channels; c++) original[c]=c%3==0 ? NAN : c%3==1 ? INFINITY : -INFINITY;
        }
        memcpy(input,original,count*sizeof(float)); out.data=pass==1 ? input : output;
        capture.calls=0; capture.error=pass==4 ? -77 : 0;
        CHECK(api->process(handle,&ctx) == capture.error && capture.calls == 1 && out.frame_count == frames);
        for (unsigned f=0; f<frames; f++) for (unsigned c=0; c<channels; c++) {
            size_t i=(size_t)f*channels+c;
            float delta=original[i]-history[c];
            float limit=delta > 0 ? rise : fall;
            if (delta > limit) delta=limit;
            else if (delta < -limit) delta=-limit;
            float want=history[c]+delta, got=((float*)out.data)[i]; history[c]=want;
            CHECK(isnan(want) ? isnan(got) : !memcmp(&want,&got,sizeof(float)));
        }
        for (unsigned c=0; c<channels; c++)
            CHECK(isnan(history[c]) ? isnan(state.algorithm.prev[c])
                : !memcmp(history+c,state.algorithm.prev+c,sizeof(float)));
        CHECK(isnan(history[0]) ? isnan(capture.seen) : !memcmp(history,&capture.seen,sizeof(float)));
    }
    orpheus_builtin_noise_slew_State saved=state;
    OrpheusValue rate={.type=ORPHEUS_VALUE_FLOAT, .value.f32=0};
    CHECK(api->set_parameter(handle,"rise_rate",&rate) == ORPHEUS_ERR_INVALID_ARG
        && !memcmp(&state,&saved,sizeof(state)));
    rate.value.f32=INFINITY;
    CHECK(api->set_parameter(handle,"fall_rate",&rate) == ORPHEUS_ERR_INVALID_ARG
        && !memcmp(&state,&saved,sizeof(state)));
    out.data=output; capture.calls=0; capture.error=0; in.frame_count=frames-1;
    CHECK(api->process(handle,&ctx) == ORPHEUS_ERR_INVALID_ARG && !capture.calls); in.frame_count=frames;
    out.frame_capacity=frames-1;
    CHECK(api->process(handle,&ctx) == ORPHEUS_ERR_INVALID_ARG && !capture.calls); out.frame_capacity=frames;
    if (count > 1) {
        out.data=input+1; CHECK(api->process(handle,&ctx) == ORPHEUS_ERR_INVALID_ARG && !capture.calls);
    }
    out.data=output; ctx.frame_count=0;
    CHECK(api->process(handle,&ctx) == 0 && !capture.calls && !memcmp(&state,&saved,sizeof(state)));
    ctx.frame_count=frames; outputs[0]=NULL;
    CHECK(api->process(handle,&ctx) == ORPHEUS_ERR_INVALID_ARG && !capture.calls);
    outputs[0]=&out; inputs[0]=NULL;
    CHECK(api->process(handle,&ctx) == ORPHEUS_ERR_INVALID_ARG && !capture.calls); inputs[0]=&in;
    params[0].value.i32=33;
    CHECK(api->prepare(handle,&config) == ORPHEUS_ERR_INVALID_ARG && !memcmp(&state,&saved,sizeof(state)));
    params[0].value.i32=channels;
    CHECK(api->reset(handle) == 0 && state.rise_rate == 4 && state.fall_rate == 8);
    for (unsigned c=0; c<32; c++) CHECK(state.algorithm.prev[c] == 0);
    CHECK(api->prepare(handle,&config) == 0 && state.rise_rate == 1 && state.fall_rate == 2);
    CHECK(api->destroy(handle) == 0);
    config.state_block=NULL; CHECK(api->create(&handle,&config) == ORPHEUS_ERR_INVALID_ARG);
    free(original); free(input); free(output);
    return 0;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    CHECK(shape(api,1,1,100) == 0 && shape(api,3,17,48000) == 0
        && shape(api,32,65536,48000) == 0 && shape(api,2,7,0) == 0);
    return 0;
}
