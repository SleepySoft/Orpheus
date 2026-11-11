#include "component_state.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const OrpheusComponentInterface* test_block_split(void);
const OrpheusComponentInterface* test_circular_buffer(void);

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "音频适配断言失败：%s:%d: %s\n", __FILE__, __LINE__, #condition); \
    return 1; } } while (0)

static OrpheusValue integer(int value) {
    OrpheusValue result = {.type=ORPHEUS_VALUE_INT, .value.i32=value};
    return result;
}

static unsigned split_emits;
static bool split_fail_allocate;
void* block_split_test_calloc(size_t count,size_t size) {
    return split_fail_allocate ? NULL : calloc(count,size);
}
static unsigned window_allocation, window_fail_allocation;
void* circular_buffer_test_calloc(size_t count,size_t size) {
    return ++window_allocation==window_fail_allocation ? NULL : calloc(count,size);
}
static uint64_t split_frame;
static uint32_t split_flags;
static int split_error;
static int split_delivery(void* user,uint32_t port,const OrpheusDelivery* delivery) {
    (void)user;++split_emits;
    if(port!=1 || !delivery || !delivery->buffer || delivery->buffer->frame_count!=32 ||
       delivery->frame_index!=split_frame || delivery->epoch!=9 || delivery->timeline_flags!=split_flags)
        return ORPHEUS_ERR_INVALID_ARG;
    return split_error;
}
static int window_delivery(void* user,uint32_t port,const OrpheusDelivery* delivery) {
    (void)user;
    if(port!=1 || !delivery || !delivery->buffer || delivery->buffer->frame_count!=512 ||
       delivery->frame_index!=17 || delivery->epoch!=7 || delivery->timeline_flags!=ORPHEUS_TIMELINE_EOS)
        return ORPHEUS_ERR_INVALID_ARG;
    return -77;
}
static int test_split(void) {
    const OrpheusComponentInterface* api = test_block_split();
    orpheus_builtin_block_split_State state = {0};
    const char* ids[] = {"factor"};
    OrpheusValue values[] = {integer(4)};
    OrpheusConfig cfg = {.sample_rate=48000, .block_size=32, .channels=2,
        .param_ids=ids, .param_values=values, .param_count=1, .state_block=&state};
    void* handle = NULL;
    CHECK(api->create(&handle, &cfg) == ORPHEUS_OK);
    CHECK(api->prepare(handle, &cfg) == ORPHEUS_OK);
    float input[256], output[64];
    OrpheusBuffer in = {.data=input, .format=ORPHEUS_FORMAT_F32, .channels=2,
        .frame_capacity=128, .frame_count=128, .interleaved=true};
    OrpheusBuffer out = {.data=output, .format=ORPHEUS_FORMAT_F32, .channels=2,
        .frame_capacity=32, .interleaved=true};
    const OrpheusBuffer* inputs[] = {&in,NULL};
    OrpheusBuffer* outputs[] = {&out};
    OrpheusContinuation continuation={.emit=split_delivery};
    OrpheusProcessContext ctx = {.inputs=inputs, .outputs=outputs, .input_count=2,
        .output_count=1, .frame_count=32, .sample_rate=48000,.continuation=&continuation};
    for (uint32_t round = 0; round < 2; ++round) {
        ctx.trigger_input=1;
        CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_PROCESSING && state.underflows==1);
        for (uint32_t packet = 0; packet < 4; ++packet) {
            for (uint32_t f = 0; f < 128; ++f) {
                input[2*f] = (float)(packet*128 + f + 1);
                input[2*f+1] = -input[2*f];
            }
            ctx.trigger_input=0;ctx.frame_index=packet*128;ctx.epoch=9;
            ctx.timeline_flags=packet==0 ? ORPHEUS_TIMELINE_DISCONTINUITY : packet==3 ? ORPHEUS_TIMELINE_EOS : 0;
            unsigned before=split_emits;
            CHECK(api->process(handle,&ctx)==ORPHEUS_OK && state.available_frames==128);
            CHECK(split_emits==before);
            for(unsigned n=0;n<256;++n) input[n]=-999.0f;
            CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_PROCESSING && state.available_frames==128);
            CHECK(split_emits==before);
            ctx.trigger_input=1;
            ctx.frame_index=9999;ctx.epoch=123;ctx.timeline_flags=ORPHEUS_TIMELINE_DROPPED;
            out.data=state.algorithm.block+1;
            CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && state.available_frames==128);
            out.data=output;
            for(uint32_t part=0;part<4;++part) {
                split_frame=packet*128+part*32;
                split_flags=packet==0 && part==0 ? ORPHEUS_TIMELINE_DISCONTINUITY :
                    packet==3 && part==3 ? ORPHEUS_TIMELINE_EOS : 0;
                CHECK(api->process(handle,&ctx)==ORPHEUS_OK);
                CHECK(state.available_frames==(int32_t)(3-part)*32);
                for (uint32_t f = 0; f < 32; ++f) {
                    const float expected=(float)(packet*128+part*32+f+1);
                    CHECK(output[2*f] == expected && output[2*f+1] == -expected);
                }
            }
        }
        CHECK(api->reset(handle) == ORPHEUS_OK);
    }
    state.underflows=INT32_MAX;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_PROCESSING && state.underflows==INT32_MAX);
    CHECK(api->reset(handle)==ORPHEUS_OK);
    for(unsigned f=0;f<128;++f) {input[2*f]=(float)(1000+f);input[2*f+1]=-input[2*f];}
    ctx.trigger_input=0;ctx.frame_index=1000;ctx.epoch=9;
    ctx.timeline_flags=ORPHEUS_TIMELINE_DISCONTINUITY|ORPHEUS_TIMELINE_EOS;
    in.data=&state.channels;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !state.available_frames);
    in.data=input;
    ctx.frame_index=UINT64_MAX-64;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !state.available_frames);
    ctx.frame_index=1000;
    CHECK(api->process(handle,&ctx)==ORPHEUS_OK);
    ctx.trigger_input=1;split_frame=1000;split_flags=ORPHEUS_TIMELINE_DISCONTINUITY;split_error=-77;
    CHECK(api->process(handle,&ctx)==-77 && state.available_frames==96);
    split_frame=1032;split_flags=0;split_error=0;
    CHECK(api->process(handle,&ctx)==ORPHEUS_OK && state.available_frames==64 && output[0]==1032.0f);
    unsigned before=split_emits;ctx.frame_count=0;
    CHECK(api->process(handle,&ctx)==ORPHEUS_OK && state.available_frames==64 && split_emits==before);
    ctx.frame_count=32;
    OrpheusValue changed=integer(16);
    CHECK(api->set_parameter(handle,"output_frames",&changed)==ORPHEUS_ERR_UNSUPPORTED && state.output_frames==32);
    CHECK(api->set_parameter(handle,"available_frames",&changed)==ORPHEUS_ERR_UNSUPPORTED && state.available_frames==64);
    state.overflows=INT32_MAX;ctx.trigger_input=0;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_PROCESSING && state.overflows==INT32_MAX && state.available_frames==64);
    float* old_block=state.algorithm.block;
    values[0]=integer(65);
    CHECK(api->prepare(handle,&cfg)==ORPHEUS_ERR_INVALID_ARG && state.algorithm.block==old_block);
    values[0]=integer(4);split_fail_allocate=true;
    CHECK(api->prepare(handle,&cfg)==ORPHEUS_ERR_OUT_OF_MEMORY && !state.algorithm.block);
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_PROCESSING);
    split_fail_allocate=false;
    CHECK(api->prepare(handle,&cfg)==ORPHEUS_OK && state.algorithm.block && !state.available_frames);
    CHECK(api->destroy(handle) == ORPHEUS_OK);
    return 0;
}

static int test_window(void) {
    const OrpheusComponentInterface* api = test_circular_buffer();
    void* state = calloc(1,api->get_descriptor()->state_size);
    CHECK(state);
    const char* ids[] = {"frame_size", "hop_size", "num_frames"};
    OrpheusValue values[] = {integer(256), integer(128), integer(1)};
    OrpheusConfig cfg = {.sample_rate=3000, .block_size=7, .channels=2,
        .param_ids=ids, .param_values=values, .param_count=3, .state_block=state};
    void* handle = NULL;
    CHECK(api->create(&handle, &cfg) == ORPHEUS_OK);
    CHECK(api->prepare(handle, &cfg) == ORPHEUS_OK);
    float input[512], output[1024];
    OrpheusBuffer in = {.data=input, .format=ORPHEUS_FORMAT_F32, .channels=2,
        .frame_capacity=128, .frame_count=128, .interleaved=true};
    OrpheusBuffer out = {.data=output, .format=ORPHEUS_FORMAT_F32, .channels=2,
        .frame_capacity=256, .interleaved=true};
    const OrpheusBuffer* inputs[] = {&in};
    OrpheusBuffer* outputs[] = {&out};
    OrpheusProcessContext ctx = {.inputs=inputs, .outputs=outputs, .input_count=1,
        .output_count=1, .frame_count=7, .sample_rate=3000};
    for (uint32_t round = 0; round < 2; ++round) {
        for (uint32_t block = 0; block < 3; ++block) {
            for (uint32_t f = 0; f < 128; ++f) {
                input[2*f] = (float)(block*128+f+1);
                input[2*f+1] = -input[2*f];
            }
            CHECK(api->process(handle, &ctx) == ORPHEUS_OK && out.frame_count == 256);
            for (uint32_t f = 0; f < 256; ++f) {
                const float expected = block == 0 && f < 128 ? 0.0f :
                    (float)((int32_t)block*128 + (int32_t)f - 127);
                CHECK(output[2*f] == expected && output[2*f+1] == -expected);
            }
        }
        CHECK(api->reset(handle) == ORPHEUS_OK);
    }
    out.frame_capacity = 128;
    CHECK(api->process(handle, &ctx) == ORPHEUS_ERR_INVALID_ARG);
    values[2] = integer(2);
    in.frame_capacity = in.frame_count = 256;
    out.frame_capacity = 512;
    CHECK(api->prepare(handle, &cfg) == ORPHEUS_OK);
    for (uint32_t f = 0; f < 256; ++f) {
        input[2*f] = (float)(f+1);
        input[2*f+1] = -input[2*f];
    }
    CHECK(api->process(handle, &ctx) == ORPHEUS_OK && out.frame_count == 512);
    for (uint32_t f = 0; f < 512; ++f) {
        const float expected = f < 128 ? 0.0f : (float)(f < 256 ? f-127 : f-255);
        CHECK(output[2*f] == expected && output[2*f+1] == -expected);
    }
    values[1] = integer(257);
    CHECK(api->prepare(handle, &cfg) == ORPHEUS_ERR_INVALID_ARG);
    values[1] = integer(128);
    for (unsigned failure=1;failure<=2;failure++) {
        window_allocation=0;window_fail_allocation=failure;
        CHECK(api->prepare(handle,&cfg)==ORPHEUS_ERR_OUT_OF_MEMORY);
        CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_PROCESSING);
        window_fail_allocation=0;
        CHECK(api->prepare(handle,&cfg)==ORPHEUS_OK);
    }
    out.data=input;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    out.data=output;
    for(unsigned f=0;f<256;f++) {input[2*f]=1000.0f+f;input[2*f+1]=-input[2*f];}
    OrpheusContinuation continuation={.emit=window_delivery};
    ctx.continuation=&continuation;ctx.frame_index=17;ctx.epoch=7;ctx.timeline_flags=ORPHEUS_TIMELINE_EOS;
    CHECK(api->process(handle,&ctx)==-77);
    ctx.continuation=NULL;memset(input,0,sizeof(input));
    CHECK(api->process(handle,&ctx)==ORPHEUS_OK);
    for(unsigned f=0;f<128;f++) CHECK(output[2*f]==1128.0f+f && output[2*f+1]==-output[2*f]);
    for(unsigned f=0;f<512;f++) input[f]=999.0f;
    ctx.frame_count=0;
    CHECK(api->process(handle,&ctx)==ORPHEUS_OK);
    ctx.frame_count=7;memset(input,0,sizeof(input));
    CHECK(api->process(handle,&ctx)==ORPHEUS_OK);
    for(unsigned f=0;f<1024;f++) CHECK(output[f]==0);
    CHECK(api->destroy(handle) == ORPHEUS_OK);
    free(state);
    return 0;
}

int main(void) {
    CHECK(test_split() == 0);
    CHECK(test_window() == 0);
    return 0;
}
