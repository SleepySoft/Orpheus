#include "component_state.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%d: %s\n", __LINE__, #expr); return 1; } } while (0)

typedef struct Capture {
    OrpheusBuffer* output;
    orpheus_builtin_sine_mod_State* state;
    float phase;
    unsigned calls;
    int error;
} Capture;
static int emit(void* user, uint32_t port, const OrpheusDelivery* delivery) {
    Capture* capture=user;
    if (port!=1 || !delivery || delivery->buffer!=capture->output) return ORPHEUS_ERR_PROCESSING;
    capture->calls++;capture->phase=capture->state->algorithm.phase;
    return capture->error;
}
static int am(const OrpheusComponentInterface* api, unsigned channels, unsigned frames, double rate) {
    size_t count=(size_t)channels*frames,bytes=count*sizeof(float);
    float* input=malloc(bytes+sizeof(float)),*output=malloc(bytes+sizeof(float)),*original=malloc(bytes);
    CHECK(input && output && original);
    orpheus_builtin_sine_mod_State state;
    const char* ids[]={"freq_hz","depth"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_FLOAT,.value.f32=7},
        {.type=ORPHEUS_VALUE_FLOAT,.value.f32=.25f}};
    OrpheusConfig config={.channels=channels,.block_size=frames,.sample_rate=rate,.state_block=&state,
        .param_count=2,.param_ids=ids,.param_values=params};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0);
    OrpheusBuffer in={.data=input,.channels=channels,.frame_count=frames,.frame_capacity=frames,
        .format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer out=in;out.data=output;
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&out};
    Capture capture={.output=&out,.state=&state};
    OrpheusContinuation continuation={.emit=emit,.user=&capture};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,
        .frame_count=frames,.continuation=&continuation};
    float phase=0,frequency=7,depth=.25f,sr=rate>0 ? (float)rate : 48000.0f;
    for (unsigned pass=0;pass<6;pass++) {
        if (pass==2) {
            frequency=20;depth=.75f;
            OrpheusValue value={.type=ORPHEUS_VALUE_FLOAT,.value.f32=frequency};
            CHECK(api->set_parameter(handle,"freq_hz",&value)==0 && state.algorithm.phase==phase);
            value.value.f32=depth;CHECK(api->set_parameter(handle,"depth",&value)==0);
        }
        float increment=6.283185307179586f*frequency/sr;
        for (size_t i=0;i<count;i++) original[i]=((int)((i*13+pass*11)%101)-50)*.03125f;
        if (pass==5) {
            original[0]=INFINITY;
            if (count>1) original[1]=NAN;
        }
        memcpy(input,original,bytes);input[count]=output[count]=123.25f;
        out.data=pass==1 ? input : output;capture.calls=0;capture.error=pass==4 ? -77 : 0;
        CHECK(api->process(handle,&ctx)==capture.error && capture.calls==1 && out.frame_count==frames);
        for (size_t i=0;i<count;i++) {
            float mod=1+depth*sinf(phase);if (mod<0) mod=0;
            float expected=original[i]*mod,actual=((float*)out.data)[i];
            CHECK(isnan(expected) ? isnan(actual) : !memcmp(&actual,&expected,sizeof(float)));
            phase+=increment;if (phase>=6.283185307179586f) phase-=6.283185307179586f;
        }
        CHECK(state.algorithm.phase==phase && capture.phase==phase
            && input[count]==123.25f && output[count]==123.25f);
    }
    orpheus_builtin_sine_mod_State saved=state;
    capture.calls=0;capture.error=0;in.frame_count=frames-1;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !capture.calls);in.frame_count=frames;
    out.frame_capacity=frames-1;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !capture.calls);out.frame_capacity=frames;
    if (count>1) {
        out.data=input+1;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !capture.calls);
        out.data=input;in.data=input+1;
        CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !capture.calls);in.data=input;
    }
    out.data=output;inputs[0]=NULL;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !capture.calls);inputs[0]=&in;
    outputs[0]=NULL;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !capture.calls);
    ctx.frame_count=0;CHECK(api->process(handle,&ctx)==0 && !capture.calls);
    OrpheusValue invalid={.type=ORPHEUS_VALUE_FLOAT,.value.f32=INFINITY};
    CHECK(api->set_parameter(handle,"freq_hz",&invalid)==ORPHEUS_ERR_INVALID_ARG);
    config.channels=33;CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(!memcmp(&saved,&state,sizeof(state)));
    CHECK(api->reset(handle)==0 && state.algorithm.phase==0 && state.freq_hz==1 && state.depth==1);
    CHECK(state.algorithm.phase_inc==6.283185307179586f/sr);
    CHECK(api->destroy(handle)==0);
    config.channels=channels;config.state_block=NULL;
    CHECK(api->create(&handle,&config)==ORPHEUS_ERR_INVALID_ARG);
    free(input);free(output);free(original);
    return 0;
}

int main(void) {
    const OrpheusComponentInterface* api = orpheus_get_interface();
    CHECK(am(api,1,1,100)==0 && am(api,3,17,48000)==0
        && am(api,32,65536,48000)==0 && am(api,2,7,0)==0);
    orpheus_builtin_sine_mod_State state;
    const char* keys[] = {"modulation_mode", "table_length", "carrier_table"};
    OrpheusValue values[] = {
        {.type = ORPHEUS_VALUE_INT, .value.i32 = 1},
        {.type = ORPHEUS_VALUE_INT, .value.i32 = 5},
        {.type = ORPHEUS_VALUE_STRING, .value.str = "0, -0, -1, 0.5, 1"}
    };
    OrpheusConfig config = {.channels = 3, .sample_rate = 48000, .block_size = 32,
        .param_count = 3, .param_ids = keys, .param_values = values, .state_block=&state};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0);
    CHECK(api->prepare(&state, &config) == ORPHEUS_OK);
    float input[32*3], output[32*3];
    OrpheusBuffer in = {.data = input,.frame_count=32,.frame_capacity=32,.channels=3,
        .format=ORPHEUS_FORMAT_F32,.interleaved=true}, out=in;
    out.data=output;
    const OrpheusBuffer* inputs[] = {&in};
    OrpheusBuffer* outputs[] = {&out};
    OrpheusProcessContext context = {.inputs = inputs, .outputs = outputs,.input_count=1,.output_count=1};
    const float table[]={0,-0.0f,-1,.5f,1};
    const unsigned blocks[] = {3, 7, 32, 1};
    unsigned absolute = 0;
    for (unsigned block = 0; block < 4; ++block) {
        context.frame_count = blocks[block];
        in.frame_count=context.frame_count;
        for (unsigned n = 0; n < context.frame_count; ++n)
            for (unsigned c = 0; c < 3; ++c) input[n*3+c] = (float)(absolute+n+c+1);
        CHECK(api->process(&state, &context) == ORPHEUS_OK && out.frame_count == context.frame_count);
        for (unsigned n = 0; n < context.frame_count; ++n)
            for (unsigned c = 0; c < 3; ++c) {
                float expected = input[n*3+c] * table[(absolute+n)%5];
                CHECK(memcmp(&expected, &output[n*3+c], sizeof(float)) == 0);
            }
        absolute += context.frame_count;
    }
    state.freq_hz = 17;
    state.depth = 0.25f;
    CHECK(api->reset(&state) == ORPHEUS_OK && state.algorithm.table_index == 0);
    CHECK(state.freq_hz == 17 && state.depth == 0.25f && state.table_length == 5);
    context.frame_count = 3;
    in.frame_count=3;
    input[0] = INFINITY; input[3] = 2; input[6] = 2;
    CHECK(api->process(&state, &context) == ORPHEUS_OK);
    CHECK(isnan(output[0]) && output[3] == 0 && signbit(output[3]) && output[6] == -2);
    CHECK(api->reset(&state) == ORPHEUS_OK);
    out.data = input;
    input[0] = 2; input[3] = 2; input[6] = 2;
    CHECK(api->process(&state, &context) == ORPHEUS_OK);
    CHECK(input[0] == 0 && input[3] == 0 && signbit(input[3]) && input[6] == -2);
    const char* bad[] = {"", "1,2", "1,2,3,4,5,6", "1,2,3,4,5,", "1,,3,4,5",
                         "nan,2,3,4,5", "inf,2,3,4,5", "1x,2,3,4,5", "1e99,2,3,4,5"};
    orpheus_builtin_sine_mod_State saved=state;
    for (unsigned n = 0; n < sizeof(bad)/sizeof(bad[0]); ++n) {
        values[2].value.str = bad[n];
        int rc=api->prepare(&state,&config);
        if (rc!=ORPHEUS_ERR_INVALID_ARG) fprintf(stderr,"周期表校验结果：%s -> %d\n",bad[n],rc);
        CHECK(rc == ORPHEUS_ERR_INVALID_ARG);
        CHECK(!memcmp(&saved,&state,sizeof(state)));
    }
    values[1].value.i32 = 1025;
    CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    values[1].value.i32 = 0;
    CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    values[1].type = ORPHEUS_VALUE_FLOAT;
    CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    config.param_count = 0;
    CHECK(api->prepare(&state, &config) == ORPHEUS_OK && state.modulation_mode == 0);
    out.data = output;
    context.frame_count = 3;
    for (unsigned n = 0; n < 9; ++n) input[n] = 0.5f;
    float phase = 0;
    CHECK(api->process(&state, &context) == ORPHEUS_OK);
    for (unsigned n = 0; n < 9; ++n) {
        float expected = input[n] * (1.0f + sinf(phase));
        CHECK(output[n] == expected);
        phase += state.algorithm.phase_inc;
    }
    CHECK(api->destroy(&state)==0);
    return 0;
}
