#include "component_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%d: %s\n", __LINE__, #expr); return 1; } } while (0)

typedef struct { unsigned calls,frames; } Capture;
static int downstream(void* user,uint32_t port,const OrpheusDelivery* delivery) {
    Capture* capture=user;
    if(port!=1 || !delivery || !delivery->buffer || delivery->buffer->frame_count!=capture->frames)
        return ORPHEUS_ERR_INVALID_ARG;
    return ++capture->calls==3 ? ORPHEUS_ERR_PROCESSING : ORPHEUS_OK;
}
typedef struct { unsigned seen;bool fail; } Slots;
static OrpheusSlotId slot(void* user,const OrpheusSlotInfo* info) {
    Slots* slots=user;
    if(strcmp(info->key,"delays_i32")==0) {
        if(info->type!=ORPHEUS_VALUE_INT || info->count!=64 || info->min_i32!=0 ||
           info->max_i32!=192000 || !(info->flags&ORPHEUS_SLOT_DOUBLE_BUFFERED) ||
           !(info->flags&ORPHEUS_SLOT_READBACK)) return ORPHEUS_SLOT_ID_INVALID;
        slots->seen++;
        if(slots->fail) return ORPHEUS_SLOT_ID_INVALID;
    }
    return 0;
}

static int largest_pool(const OrpheusComponentInterface* api) {
    const unsigned channels=64,frames=65536,capacity=192000;
    const size_t bytes=(size_t)channels*frames*sizeof(float);
    float* input=malloc(bytes),*output=malloc(bytes);
    float* chronological=calloc(capacity,sizeof(float)),*saved=malloc(capacity*sizeof(float));
    CHECK(input && output && chronological && saved);
    const char* ids[]={"channels","pool_size_samples"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=64},
        {.type=ORPHEUS_VALUE_INT,.value.i32=192000}};
    orpheus_builtin_pool_delay_State state={0};
    OrpheusConfig config={.block_size=frames,.param_count=2,.param_ids=ids,.param_values=params,.state_block=&state};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && handle==&state && api->prepare(handle,&config)==0);
    Slots slots={0};OrpheusRegistry reg={.ctx=&slots,.add=slot};
    CHECK(api->register_slots(handle,&reg)==0 && slots.seen==1);
    slots.fail=true;
    CHECK(api->register_slots(handle,&reg)==ORPHEUS_ERR_PROCESSING);
    for(unsigned c=0;c<channels;c++) state.delays_i32[c]=capacity;
    OrpheusBuffer in={.data=input,.channels=channels,.format=ORPHEUS_FORMAT_F32,.interleaved=true,
        .frame_count=frames,.frame_capacity=frames};
    OrpheusBuffer out=in;out.data=output;
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&out};
    Capture capture={.frames=frames};
    OrpheusContinuation continuation={.emit=downstream,.user=&capture};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,
        .frame_count=frames,.continuation=&continuation};
    const uint32_t patterns[]={0x80000000u,0x7fa12345u,0x7fc76543u,0x7f800000u,0xff800000u,1,0x3f800000u};
    for(unsigned pass=0;pass<4;pass++) {
        for(unsigned n=0;n<frames;n++) for(unsigned c=0;c<channels;c++)
            memcpy(input+(size_t)n*channels+c,patterns+(n+c+pass)%7,sizeof(float));
        CHECK(api->process(handle,&ctx)==(pass==2 ? ORPHEUS_ERR_PROCESSING : ORPHEUS_OK));
        CHECK(capture.calls==pass+1 && state.algorithm.cursor==((pass+1)*frames)%capacity);
        for(unsigned n=0;n<frames;n++) for(unsigned c=0;c<channels;c++)
            CHECK(!memcmp(output+(size_t)n*channels+c,chronological+n,sizeof(float)));
        memmove(chronological,chronological+frames,(capacity-frames)*sizeof(float));
        for(unsigned n=0;n<frames;n++)
            memcpy(chronological+capacity-frames+n,input+(size_t)n*channels+channels-1,sizeof(float));
    }
    float* buffer=state.algorithm.buffer;uint32_t cursor=state.algorithm.cursor;
    memcpy(saved,buffer,capacity*sizeof(float));
    params[1].value.i32=0;
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(state.algorithm.buffer==buffer && state.algorithm.cursor==cursor &&
          !memcmp(saved,buffer,capacity*sizeof(float)) && state.delays_i32[63]==192000);
    params[1].value.i32=192000;
    in.frame_count=frames-1;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && capture.calls==4);
    in.frame_count=frames;out.data=input;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && state.algorithm.cursor==cursor);
    out.data=buffer;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !memcmp(saved,buffer,capacity*sizeof(float)));
    out.data=output;
    OrpheusValue enabled={.type=ORPHEUS_VALUE_BOOL,.value.b=false};
    CHECK(api->set_parameter(handle,"enabled",&enabled)==0);
    CHECK(api->process(handle,&ctx)==0 && !memcmp(input,output,bytes) && capture.calls==5);
    CHECK(state.algorithm.cursor==cursor && !memcmp(saved,buffer,capacity*sizeof(float)));
    ctx.frame_count=0;capture.frames=0;
    CHECK(api->process(handle,&ctx)==0 && capture.calls==6 && out.frame_count==0 && state.algorithm.cursor==cursor);
    enabled.value.b=true;
    CHECK(api->set_parameter(handle,"enabled",&enabled)==0);
    CHECK(api->process(handle,&ctx)==0 && capture.calls==7 && state.algorithm.cursor==cursor);
    CHECK(api->destroy(handle)==0);
    free(input);free(output);free(chronological);free(saved);
    return 0;
}

int main(void) {
    const OrpheusComponentInterface* api = orpheus_get_interface();
    CHECK(largest_pool(api)==0);
    orpheus_builtin_pool_delay_State state = {0};
    const char* keys[] = {"channels", "pool_size_samples"};
    OrpheusValue values[] = {
        {.type = ORPHEUS_VALUE_INT, .value.i32 = 16},
        {.type = ORPHEUS_VALUE_INT, .value.i32 = 600}
    };
    OrpheusConfig config = {.block_size=32,.state_block=&state,
        .param_count = 2, .param_ids = keys, .param_values = values};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==ORPHEUS_OK);
    CHECK(api->prepare(&state, &config) == ORPHEUS_OK);
    state.delays_i32[0] = 2;
    state.delays_i32[1] = 3;
    float input[32 * 16], output[32 * 16];
    OrpheusBuffer in = {.data = input, .channels = 16, .frame_count = 32, .frame_capacity = 32,
                        .format = ORPHEUS_FORMAT_F32, .interleaved = true};
    OrpheusBuffer out = in;
    out.data = output;
    const OrpheusBuffer* inputs[] = {&in};
    OrpheusBuffer* outputs[] = {&out};
    OrpheusProcessContext context = {.inputs = inputs, .outputs = outputs, .input_count = 1, .output_count = 1, .frame_count = 32};
    for (unsigned block = 0; block < 3; ++block) {
        for (unsigned n = 0; n < 32; ++n)
            for (unsigned c = 0; c < 16; ++c) input[n*16+c] = (float)(10000*c+32*block+n+1);
        if (block == 2) state.delays_i32[0] = 4;
        CHECK(api->process(&state, &context) == ORPHEUS_OK && out.frame_count == 32);
    }
    CHECK(output[0] == 63 && output[16] == 64 && output[32] == 10062 && output[48] == 10063);
    float saved[600];
    memcpy(saved, state.algorithm.buffer, sizeof(saved));
    uint32_t frozen_cursor = state.algorithm.cursor;
    OrpheusValue enabled = {.type = ORPHEUS_VALUE_BOOL, .value.b = false}, got;
    CHECK(api->set_parameter(&state, "enabled", &enabled) == ORPHEUS_OK);
    CHECK(api->get_parameter(&state, "enabled", &got) == ORPHEUS_OK && !got.value.b);
    for (unsigned i = 0; i < 32*16; ++i) input[i] = -(float)(i+1);
    CHECK(api->process(&state, &context) == ORPHEUS_OK && !memcmp(output, input, sizeof(input)) &&
          state.algorithm.cursor == frozen_cursor && !memcmp(saved, state.algorithm.buffer, sizeof(saved)));
    out.frame_capacity = 31;
    CHECK(api->process(&state, &context) == ORPHEUS_ERR_INVALID_ARG && state.algorithm.cursor == frozen_cursor &&
          !memcmp(saved, state.algorithm.buffer, sizeof(saved)));
    out.frame_capacity = 32;
    out.data = input+1;
    CHECK(api->process(&state, &context) == ORPHEUS_ERR_INVALID_ARG);
    out.data = output;
    enabled.value.b = true;
    CHECK(api->set_parameter(&state, "enabled", &enabled) == ORPHEUS_OK);
    CHECK(api->process(&state, &context) == ORPHEUS_OK &&
          output[0] == saved[(frozen_cursor+1)%600] && output[1] == saved[(frozen_cursor+5)%600]);
    CHECK(api->reset(&state) == ORPHEUS_OK && state.algorithm.cursor == 0);
    CHECK(state.delays_i32[0] == 4 && state.delays_i32[1] == 3);
    CHECK(api->process(&state, &context) == ORPHEUS_OK);
    CHECK(output[0] == 0 && output[16] == 0 && output[32] == 0 && output[48] == 0);
    uint32_t cursor = state.algorithm.cursor;
    state.delays_i32[1] = -1;
    CHECK(api->process(&state, &context) == ORPHEUS_ERR_INVALID_ARG && state.algorithm.cursor == cursor);
    state.delays_i32[1] = 601;
    CHECK(api->process(&state, &context) == ORPHEUS_ERR_INVALID_ARG && state.algorithm.cursor == cursor);
    state.delays_i32[1] = 3;
    context.frame_count = 3;
    CHECK(api->process(&state, &context) == ORPHEUS_OK && out.frame_count == 3);
    context.frame_count = 7;
    CHECK(api->process(&state, &context) == ORPHEUS_OK && out.frame_count == 7);
    enabled.type = ORPHEUS_VALUE_INT;
    enabled.value.i32 = 0;
    CHECK(api->set_parameter(&state, "enabled", &enabled) == ORPHEUS_ERR_INVALID_ARG && state.enabled);
    const char* bad_key[] = {"enabled"};
    OrpheusConfig bad_config = {.param_ids = bad_key, .param_values = &enabled, .param_count = 1};
    CHECK(api->prepare(&state, &bad_config) == ORPHEUS_ERR_INVALID_ARG && state.enabled && state.algorithm.buffer);
    enabled.type = ORPHEUS_VALUE_BOOL;
    enabled.value.b = false;
    CHECK(api->set_parameter(&state, "enabled", &enabled) == ORPHEUS_OK &&
          api->reset(&state) == ORPHEUS_OK && !state.enabled);
    outputs[0] = NULL;
    CHECK(api->process(&state, &context) == ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->destroy(&state) == ORPHEUS_OK && !state.algorithm.buffer);
    values[0].value.i32 = 65;
    CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    values[0].value.i32 = 1;
    values[1].value.i32 = 0;
    CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    values[1].type = ORPHEUS_VALUE_FLOAT;
    CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    return 0;
}
