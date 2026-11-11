#include "component_state.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x) do {if (!(x)) {fprintf(stderr,"序列状态断言失败：%d: %s\n",__LINE__,#x);return 1;}} while (0)
static float start[2], accepted[2], alpha, feedback[2], expected_start[2], expected_accepted[2], next[2];
static int32_t freeze, expected_freeze;
static OrpheusBuffer* published[4];
static OrpheusBuffer* feedback_buffer;
static unsigned calls, fail_port;
static bool write_feedback;

static int deliver(void* user, uint32_t port, const OrpheusDelivery* delivery) {
    (void)user;
    static const unsigned order[] = {2,3,4,1};
    if (calls >= 4 || port != order[calls++] || !delivery || delivery->buffer != published[port-1] ||
        delivery->frame_index != 42 || delivery->epoch != 7 ||
        delivery->timeline_flags != ORPHEUS_TIMELINE_DISCONTINUITY ||
        memcmp(start, expected_start, sizeof(start)) || memcmp(accepted, expected_accepted, sizeof(accepted)) ||
        alpha != .25f || freeze != expected_freeze) return ORPHEUS_ERR_PROCESSING;
    if (port == fail_port) return -77;
    if (port == 1) {
        if (write_feedback) memcpy(feedback, next, sizeof(feedback));
        feedback_buffer->frame_count = write_feedback ? 1 : 0;
    }
    return ORPHEUS_OK;
}

int main(void) {
    const OrpheusComponentInterface* api = orpheus_get_interface();
    orpheus_builtin_sequence_envelope_state_State s;
    const char* ids[] = {"channels","silent"};
    OrpheusValue values[] = {{.type=ORPHEUS_VALUE_INT,.value.i32=2},
        {.type=ORPHEUS_VALUE_FLOAT,.value.f32=.2f}};
    OrpheusConfig config = {.state_block=&s,.sample_rate=48000,.block_size=8,.channels=2,
        .param_ids=ids,.param_values=values,.param_count=2};
    void* handle = NULL;
    CHECK(api->create(&handle,&config)==0 && handle==&s && api->prepare(handle,&config)==0);
    float targets[] = {-0.0f,-1}, coefficient = .25f;
    uint32_t sequence = 1;
    OrpheusBuffer inputs[] = {
        {.data=targets,.format=ORPHEUS_FORMAT_F32,.channels=2,.frame_count=1,.frame_capacity=1,.interleaved=true},
        {.data=&sequence,.format=ORPHEUS_FORMAT_I32,.channels=1,.frame_count=1,.frame_capacity=1,.interleaved=true},
        {.data=&coefficient,.format=ORPHEUS_FORMAT_F32,.channels=1,.frame_count=1,.frame_capacity=1,.interleaved=true},
        {.data=feedback,.format=ORPHEUS_FORMAT_F32,.channels=2,.frame_count=1,.frame_capacity=1,.interleaved=true},
    };
    OrpheusBuffer outputs[] = {
        {.data=start,.format=ORPHEUS_FORMAT_F32,.channels=2,.frame_capacity=1,.interleaved=true},
        {.data=accepted,.format=ORPHEUS_FORMAT_F32,.channels=2,.frame_capacity=1,.interleaved=true},
        {.data=&alpha,.format=ORPHEUS_FORMAT_F32,.channels=1,.frame_capacity=1,.interleaved=true},
        {.data=&freeze,.format=ORPHEUS_FORMAT_I32,.channels=1,.frame_capacity=1,.interleaved=true},
    };
    const OrpheusBuffer* in[] = {inputs,inputs+1,inputs+2,inputs+3};
    for (unsigned i=0;i<4;i++) published[i]=outputs+i;
    feedback_buffer=inputs+3;
    OrpheusContinuation continuation = {.emit=deliver};
    OrpheusProcessContext ctx = {.inputs=in,.input_count=4,.outputs=published,.output_count=4,
        .frame_count=8,.sample_rate=48000,.frame_index=42,.epoch=7,
        .timeline_flags=ORPHEUS_TIMELINE_DISCONTINUITY,.continuation=&continuation};
    memcpy(expected_start,targets,sizeof(targets));
    memcpy(expected_accepted,targets,sizeof(targets));
    expected_freeze=1;next[0]=.125f;next[1]=-.5f;write_feedback=true;
    CHECK(api->process(handle,&ctx)==0 && calls==4 && s.algorithm.initialized &&
        !memcmp(s.algorithm.current,next,sizeof(next)));
    memcpy(expected_start,next,sizeof(next));
    targets[0]=10;targets[1]=11;coefficient=.75f;next[0]=.25f;next[1]=-1;calls=0;
    CHECK(api->process(handle,&ctx)==0 && calls==4 && s.algorithm.coefficient==.25f &&
        !memcmp(s.algorithm.targets,expected_accepted,sizeof(expected_accepted)));
    sequence=2;expected_start[0]=.25f;expected_start[1]=.2f;
    memcpy(expected_accepted,targets,sizeof(targets));expected_freeze=0;calls=0;fail_port=3;
    next[0]=.5f;next[1]=-2;
    CHECK(api->process(handle,&ctx)==-77 && calls==2 && s.algorithm.sequence==2 &&
        !memcmp(s.algorithm.current,expected_start,sizeof(expected_start)));
    targets[0]=12;targets[1]=13;calls=0;fail_port=0;
    CHECK(api->process(handle,&ctx)==0 && calls==4 && !memcmp(s.algorithm.current,next,sizeof(next)));

    orpheus_builtin_sequence_envelope_state_State before=s;
    for (unsigned i=0;i<3;i++) {
        const float invalid[] = {-1,NAN,INFINITY};
        values[1].value.f32=invalid[i];
        CHECK(api->prepare(handle,&config)!=0 && !memcmp(&s,&before,sizeof(s)));
    }
    values[1].value.f32=.2f;
    outputs[0].data=targets;calls=0;
    CHECK(api->process(handle,&ctx)!=0 && calls==0 && !memcmp(&s,&before,sizeof(s)));
    outputs[0].data=start;ctx.trigger_input=1;
    CHECK(api->process(handle,&ctx)!=0 && calls==0 && !memcmp(&s,&before,sizeof(s)));
    ctx.trigger_input=0;ctx.frame_count=0;
    CHECK(api->process(handle,&ctx)==0 && calls==0 && !memcmp(&s,&before,sizeof(s)));
    ctx.frame_count=8;
    CHECK(api->reset(handle)==0 && !s.algorithm.initialized && s.silent==.2f && s.channels==2);
    sequence=0;coefficient=.25f;write_feedback=false;inputs[3].frame_count=1;
    memcpy(expected_start,targets,sizeof(targets));memcpy(expected_accepted,targets,sizeof(targets));
    CHECK(api->process(handle,&ctx)!=0 && calls==4 &&
        !memcmp(s.algorithm.current,targets,sizeof(targets)));
    inputs[3].frame_count=1;calls=0;write_feedback=true;
    for (unsigned i=0;i<4;i++) published[i]=NULL;
    CHECK(api->process(handle,&ctx)==0 && calls==4 && !memcmp(s.algorithm.current,next,sizeof(next)));
    CHECK(api->reset(handle)==0);
    const uint32_t target_bits[]={UINT32_C(0x80000000),UINT32_C(0x7fc12345)};
    const uint32_t coefficient_bits=UINT32_C(0xffc23456);
    memcpy(targets,target_bits,sizeof(targets));memcpy(&coefficient,&coefficient_bits,sizeof(coefficient));
    sequence=1;ctx.continuation=NULL;
    CHECK(api->process(handle,&ctx)==0 && !memcmp(s.algorithm.targets,target_bits,sizeof(targets)) &&
        !memcmp(&s.algorithm.coefficient,&coefficient_bits,sizeof(coefficient)));
    CHECK(api->destroy(handle)==0);
    puts("序列状态同步反馈、出口顺序与错误事务通过");
    return 0;
}
