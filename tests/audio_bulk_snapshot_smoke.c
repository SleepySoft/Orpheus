#include "component_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); return 1; } } while (0)
typedef struct Capture {
    const OrpheusBuffer* input;
    const OrpheusBuffer* output;
    unsigned calls;
    int error;
} Capture;

static uint32_t pattern(size_t index, unsigned pass) {
    static const uint32_t values[]={0,0x80000000u,0x7f800000u,0xff800000u,
        0x7fc12345u,0x7f812345u,1,0x3f123456u};
    uint32_t i=(uint32_t)index+pass*1048573u;
    return i%19u<8u ? values[i%19u] : 0x3f000000u|((i*2654435761u)&0x007fffffu);
}

static int emit(void* user, uint32_t port, const OrpheusDelivery* delivery) {
    Capture* capture=user;
    const OrpheusBuffer* out=delivery ? delivery->buffer : NULL;
    if (port!=1 || out!=capture->output || !out || out->frame_count!=capture->input->frame_count
        || delivery->frame_index!=123 || delivery->epoch!=7
        || delivery->timeline_flags!=ORPHEUS_TIMELINE_DISCONTINUITY
        || memcmp(out->data,capture->input->data,(size_t)out->frame_count*out->channels*sizeof(float)))
        return ORPHEUS_ERR_PROCESSING;
    capture->calls++;
    return capture->error;
}

static int shape(const OrpheusComponentInterface* api, unsigned channels, unsigned frames, int fixed) {
    size_t count=(size_t)channels*frames, bytes=count*sizeof(uint32_t);
    uint32_t* input=malloc(bytes+sizeof(uint32_t));
    uint32_t* output=malloc(bytes+sizeof(uint32_t));
    CHECK(input && output);
    const char* ids[]={"channels","elements"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=channels},
        {.type=ORPHEUS_VALUE_INT,.value.i32=fixed ? (int32_t)frames : 0}};
    orpheus_builtin_audio_bulk_snapshot_State state;
    OrpheusConfig config={.block_size=frames,.param_ids=ids,.param_values=params,
        .param_count=2,.state_block=&state};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && handle==&state && api->prepare(handle,&config)==0);
    OrpheusBuffer in={.data=input,.channels=channels,.frame_capacity=frames,.frame_count=frames,
        .format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer out=in; out.data=output; out.frame_count=0;
    const OrpheusBuffer* inputs[]={&in}; OrpheusBuffer* outputs[]={&out};
    Capture capture={.input=&in,.output=&out};
    OrpheusContinuation continuation={.emit=emit,.user=&capture};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,
        .frame_count=frames,.frame_index=123,.epoch=7,.valid_frames=frames,
        .timeline_flags=ORPHEUS_TIMELINE_DISCONTINUITY,.continuation=&continuation};
    for (unsigned pass=0;pass<4;pass++) {
        for (size_t i=0;i<count;i++) input[i]=pattern(i,pass);
        input[count]=output[count]=0xa5a5a5a5u;
        capture.calls=0;capture.error=pass==2 ? -77 : 0;
        CHECK(api->process(handle,&ctx)==capture.error && capture.calls==1 && out.frame_count==frames);
        CHECK(!memcmp(input,output,bytes) && input[count]==0xa5a5a5a5u && output[count]==0xa5a5a5a5u);
        memset(input,0,bytes);
        for (size_t i=0;i<count;i++) CHECK(output[i]==pattern(i,pass));
    }
    capture.calls=0;capture.error=0;
    orpheus_builtin_audio_bulk_snapshot_State saved=state;
    in.frame_count=frames-1;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !capture.calls); in.frame_count=frames;
    out.frame_capacity=frames-1;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !capture.calls); out.frame_capacity=frames;
    out.data=input;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !capture.calls);
    if (count>1) {
        out.data=input+1;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !capture.calls);
        in.data=input+1;out.data=input;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !capture.calls);
        in.data=input;
    }
    out.data=output;inputs[0]=NULL;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !capture.calls);inputs[0]=&in;
    in.channels=channels+1;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !capture.calls);in.channels=channels;
    outputs[0]=NULL;
    CHECK(api->process(handle,&ctx)==0 && !capture.calls);
    inputs[0]=NULL;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && !capture.calls);
    ctx.frame_count=0;
    CHECK(api->process(handle,&ctx)==0 && !capture.calls);
    ctx.frame_count=frames;inputs[0]=&in;outputs[0]=&out;
    CHECK(!memcmp(&state,&saved,sizeof(state)));
    if (frames>1) {
        ctx.frame_count=in.frame_count=frames-1;
        CHECK(api->process(handle,&ctx)==(fixed ? ORPHEUS_ERR_INVALID_ARG : 0));
        CHECK(capture.calls==(fixed ? 0u : 1u));
        ctx.frame_count=in.frame_count=frames;
    }
    params[0].value.i32=129;
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&state,&saved,sizeof(state)));
    params[0].value.i32=channels;params[1].value.i32=65537;
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&state,&saved,sizeof(state)));
    CHECK(api->reset(handle)==0 && api->destroy(handle)==0);
    config.state_block=NULL;
    CHECK(api->create(&handle,&config)==ORPHEUS_ERR_INVALID_ARG);
    free(input);free(output);
    return 0;
}

int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    CHECK(shape(api,1,1,0)==0 && shape(api,22,17,1)==0
        && shape(api,128,65536,0)==0 && shape(api,3,7,0)==0);
    return 0;
}
