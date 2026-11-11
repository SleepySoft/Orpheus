#include "component_state.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%d: %s\n", __LINE__, #expr); return 1; } } while (0)

typedef struct { unsigned slots,calls; bool fail; OrpheusSlotInfo shared,per_channel; } Capture;
static OrpheusSlotId add(void* user, const OrpheusSlotInfo* slot) {
    Capture* capture=user;
    if(!strcmp(slot->key,"coefs")) capture->shared=*slot;
    if(!strcmp(slot->key,"coefs_per_channel")) capture->per_channel=*slot;
    capture->slots++;
    return capture->fail ? ORPHEUS_SLOT_ID_INVALID : 0;
}
static int emit(void* user, uint32_t port, const OrpheusDelivery* delivery) {
    Capture* capture=user;
    if(port!=1 || !delivery || !delivery->buffer) return ORPHEUS_ERR_INVALID_ARG;
    capture->calls++;
    return capture->fail ? ORPHEUS_ERR_PROCESSING : ORPHEUS_OK;
}
static float sample(unsigned n, unsigned c) {
    return (float)((int)((n*17+c*31)%103)-51)*.03125f;
}
static int boundary(const OrpheusComponentInterface* api) {
    const unsigned channels=64,frames=65536;
    size_t count=(size_t)channels*frames,bytes=count*sizeof(float);
    float* input=malloc(bytes),*output=malloc(bytes);
    CHECK(input && output);
    char initial[32768];size_t used=0;
    for(unsigned c=0;c<channels;c++) for(unsigned k=0;k<33;k++)
        used+=(size_t)snprintf(initial+used,sizeof(initial)-used,used ? ",%.9g" : "%.9g",
            k==0 ? .25 : k==16 ? .125 : k==32 ? -.25 : 0);
    const char* ids[]={"channels","order","coefs_mode","initial_coefs_per_channel"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=64},
        {.type=ORPHEUS_VALUE_INT,.value.i32=16},{.type=ORPHEUS_VALUE_STRING,.value.str="per_channel"},
        {.type=ORPHEUS_VALUE_STRING,.value.str=initial}};
    orpheus_builtin_iir_State state;
    OrpheusConfig config={.block_size=frames,.param_count=4,.param_ids=ids,.param_values=params,.state_block=&state};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0);
    Capture capture={0};OrpheusRegistry registry={.ctx=&capture,.add=add};
    CHECK(api->register_slots(handle,&registry)==0 && capture.slots==6);
    CHECK(capture.shared.count==33 && capture.per_channel.count==2112 &&
        capture.shared.offset==offsetof(orpheus_builtin_iir_State,algorithm.shared) &&
        capture.per_channel.offset==offsetof(orpheus_builtin_iir_State,algorithm.per_channel) &&
        (capture.per_channel.flags & ORPHEUS_SLOT_DOUBLE_BUFFERED));
    capture.fail=true;
    CHECK(api->register_slots(handle,&registry)==ORPHEUS_ERR_PROCESSING);
    capture.fail=false;
    OrpheusBuffer in={.data=input,.channels=channels,.format=ORPHEUS_FORMAT_F32,.interleaved=true,
        .frame_count=frames,.frame_capacity=frames};
    OrpheusBuffer out=in;out.data=output;
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&out};
    OrpheusContinuation continuation={.emit=emit,.user=&capture};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,
        .frame_count=frames,.continuation=&continuation};
    float history[64][16]={{0}};
    for(unsigned pass=0;pass<2;pass++) {
        for(unsigned n=0;n<frames;n++) for(unsigned c=0;c<channels;c++)
            input[(size_t)n*channels+c]=sample(pass*frames+n,c);
        capture.fail=pass==0;
        CHECK(api->process(handle,&ctx)==(pass==0 ? ORPHEUS_ERR_PROCESSING : ORPHEUS_OK));
        CHECK(capture.calls==pass+1);
        for(unsigned n=0;n<frames;n++) for(unsigned c=0;c<channels;c++) {
            float x=sample(pass*frames+n,c),y=.25f*x+history[c][0];
            for(unsigned k=0;k<15;k++) history[c][k]=history[c][k+1];
            history[c][15]=.125f*x-(-.25f*y);
            CHECK(!memcmp(output+(size_t)n*channels+c,&y,sizeof(float)));
        }
    }
    float saved_history[64][16];memcpy(saved_history,state.algorithm.history,sizeof(saved_history));
    in.frame_count=frames-1;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    in.frame_count=frames;out.data=input;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(!memcmp(saved_history,state.algorithm.history,sizeof(saved_history)) && capture.calls==2);
    out.data=output;state.processing_enabled=0;inputs[0]=NULL;
    CHECK(api->process(handle,&ctx)==0 && capture.calls==3);
    CHECK(!memcmp(saved_history,state.algorithm.history,sizeof(saved_history)));
    for(size_t i=0;i<count;i++) CHECK(output[i]==0 && !signbit(output[i]));
    outputs[0]=NULL;ctx.frame_count=0;
    CHECK(api->process(handle,&ctx)==0 && capture.calls==3);
    CHECK(api->destroy(handle)==0);
    free(input);free(output);
    return 0;
}

int main(void) {
    const OrpheusComponentInterface* api = orpheus_get_interface();
    orpheus_builtin_iir_State state = {0};
    const char* keys[] = {"order", "coefs", "channels"};
    OrpheusValue values[] = {
        {.type = ORPHEUS_VALUE_INT, .value.i32 = 1},
        {.type = ORPHEUS_VALUE_STRING, .value.str = "0.5,0.25,-0.5"},
        {.type = ORPHEUS_VALUE_INT, .value.i32 = 2}
    };
    OrpheusConfig config = {.channels = 2, .block_size=2, .state_block=&state,
        .param_count = 3, .param_ids = keys, .param_values = values};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==ORPHEUS_OK);
    CHECK(api->prepare(&state, &config) == ORPHEUS_OK);
    float input[4] = {1, 2, 1, 2}, output[4];
    OrpheusBuffer in = {.data=input,.channels=2,.format=ORPHEUS_FORMAT_F32,.interleaved=true,
        .frame_count=2,.frame_capacity=2}, out = in;
    out.data=output;
    const OrpheusBuffer* inputs[] = {&in};
    OrpheusBuffer* outputs[] = {&out};
    OrpheusProcessContext context = {.inputs = inputs, .outputs = outputs, .frame_count = 1,
        .input_count=1,.output_count=1};
    CHECK(api->process(&state, &context) == ORPHEUS_OK && output[0] == 0.5f && output[1] == 1);
    memset(state.algorithm.shared, 0, sizeof(state.algorithm.shared));
    context.frame_count = 2;
    CHECK(api->process(&state, &context) == ORPHEUS_OK);
    CHECK(output[0] == 0.5f && output[1] == 1 && output[2] == 0 && output[3] == 0);
    CHECK(api->prepare(&state, &config) == ORPHEUS_OK);
    context.frame_count = 1;
    CHECK(api->process(&state, &context) == ORPHEUS_OK);
    float frozen[64][16];
    memcpy(frozen, state.algorithm.history, sizeof(frozen));
    OrpheusValue enabled = {.type = ORPHEUS_VALUE_INT, .value.i32 = 0};
    CHECK(api->set_parameter(&state, "processing_enabled", &enabled) == ORPHEUS_OK);
    inputs[0] = NULL;
    CHECK(api->process(&state, &context) == ORPHEUS_OK);
    CHECK(output[0] == 0 && !signbit(output[0]) && output[1] == 0 && !signbit(output[1]));
    CHECK(memcmp(frozen, state.algorithm.history, sizeof(frozen)) == 0);
    CHECK(api->reset(&state) == ORPHEUS_OK && state.processing_enabled == 0 && state.algorithm.shared[0] == 0.5f);
    CHECK(state.algorithm.history[0][0] == 0);
    const char* invalid[] = {"", "1,0", "1,0,0,1", "1,0,0,", "1,nan,0", "1,inf,0", "1,0,0suffix"};
    for (unsigned n = 0; n < sizeof(invalid)/sizeof(invalid[0]); ++n) {
        orpheus_builtin_iir_State saved=state;
        values[1].value.str = invalid[n];
        CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
        CHECK(!memcmp(&state,&saved,sizeof(state)));
    }
    char coefs[256] = "1";
    for (unsigned n = 0; n < 32; ++n) strcat(coefs, ",0");
    values[0].value.i32 = 16; values[1].value.str = coefs; values[2].value.i32 = 64;
    CHECK(api->prepare(&state, &config) == ORPHEUS_OK);
    float wide_in[64] = {0}, wide_out[64];
    wide_in[63] = 1; in.data = wide_in; out.data = wide_out; inputs[0] = &in;
    in.channels=out.channels=64;
    CHECK(api->process(&state, &context) == ORPHEUS_OK && wide_out[63] == 1);
    for (unsigned n = 0; n < 63; ++n) CHECK(wide_out[n] == 0);
    values[0].value.i32 = 17;
    CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    values[0].value.i32 = 16; values[2].value.i32 = 65;
    CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->destroy(handle)==ORPHEUS_OK);
    CHECK(boundary(api)==0);
    return 0;
}
