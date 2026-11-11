#include "component_state.h"
#ifdef calloc
#undef calloc
#endif
#ifdef free
#undef free
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%d: %s\n", __LINE__, #expr); return 1; } } while (0)
typedef orpheus_builtin_nlms_State State;
static unsigned allocations,live,fail_at;

void* nlms_test_calloc(size_t count, size_t size) {
    if (++allocations==fail_at) return NULL;
    void* ptr=calloc(count,size);
    if (ptr) ++live;
    return ptr;
}

void nlms_test_free(void* ptr) {
    if (ptr) --live;
    free(ptr);
}

static int allocation_failures(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    for (unsigned failure=1;failure<=3;++failure) {
        State state;
        OrpheusConfig config={.channels=2,.state_block=&state};
        void* handle=NULL;
        CHECK(api->create(&handle,&config)==0 && handle==&state);
        allocations=0;fail_at=failure;
        CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_OUT_OF_MEMORY && live==0 &&
            !state.algorithm.w && !state.algorithm.x && !state.algorithm.pos);
        fail_at=0;
        CHECK(api->prepare(handle,&config)==0 && live==3);
        CHECK(api->destroy(handle)==0 && live==0);
        CHECK(api->destroy(handle)==0 && live==0);
    }
    return 0;
}

typedef struct { unsigned calls; int error; } Delivery;
static int emit(void* user, uint32_t port, const OrpheusDelivery* delivery) {
    Delivery* result=user;
    if (port!=1 || !delivery || !delivery->buffer || delivery->frame_index!=333 ||
        delivery->epoch!=8 || delivery->buffer->frame_count!=3)
        return ORPHEUS_ERR_INVALID_ARG;
    ++result->calls;
    return result->error;
}

static int numerical_and_errors(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    State state;
    const char* ids[]={"filter_length","step_size","leakage","eps"};
    OrpheusValue params[]={
        {.type=ORPHEUS_VALUE_INT,.value.i32=3},
        {.type=ORPHEUS_VALUE_FLOAT,.value.f32=.5f},
        {.type=ORPHEUS_VALUE_FLOAT,.value.f32=.75f},
        {.type=ORPHEUS_VALUE_FLOAT,.value.f32=.125f}};
    OrpheusConfig config={.channels=2,.param_count=4,.param_ids=ids,.param_values=params,
        .state_block=&state};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0 && live==3);
    float ref[7],error[7],output[7],weights[2][3]={{0}},history[2][3]={{0}};
    OrpheusBuffer in={.data=ref,.channels=2,.format=ORPHEUS_FORMAT_F32,.interleaved=true,
        .frame_count=3,.frame_capacity=3},err=in,out=in;
    err.data=error;out.data=output;
    const OrpheusBuffer* inputs[]={&in,&err};OrpheusBuffer* outputs[]={&out};
    Delivery result={0};
    OrpheusContinuation continuation={.user=&result,.emit=emit};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=2,.output_count=1,
        .frame_count=3,.valid_frames=3,.frame_index=333,.epoch=8,.continuation=&continuation};
    for (unsigned pass=0;pass<3;++pass) {
        for (unsigned i=0;i<6;++i) {
            ref[i]=(float)((int)((i+pass*6)*7%17)-8)*.125f;
            error[i]=(float)((int)((i+pass*6)*3%11)-5)*.25f;
        }
        inputs[1]=pass==2 ? NULL : &err;
        result.error=pass==0 ? -77 : 0;
        CHECK(api->process(handle,&ctx)==result.error && result.calls==pass+1);
        for (unsigned c=0;c<2;++c) for (unsigned n=0;n<3;++n) {
            for (unsigned k=2;k>0;--k) history[c][k]=history[c][k-1];
            history[c][0]=ref[n*2+c];
            float value=0,norm=0;
            for (unsigned k=0;k<3;++k) {
                value+=weights[c][k]*history[c][k];
                norm+=history[c][k]*history[c][k];
            }
            CHECK(!memcmp(output+n*2+c,&value,sizeof(float)));
            if (inputs[1]) {
                float scale=.5f*error[n*2+c]/(norm+.125f);
                for (unsigned k=0;k<3;++k)
                    weights[c][k]=.75f*weights[c][k]+scale*history[c][k];
            }
        }
        CHECK(!memcmp(state.algorithm.w,weights,sizeof(weights)));
    }
    State snapshot=state;
    float saved_w[6],saved_x[6];uint32_t saved_pos[2];
    memcpy(saved_w,state.algorithm.w,sizeof(saved_w));
    memcpy(saved_x,state.algorithm.x,sizeof(saved_x));
    memcpy(saved_pos,state.algorithm.pos,sizeof(saved_pos));
    params[0].value.i32=4097;
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG &&
        !memcmp(&snapshot,&state,sizeof(state)) && live==3);
    params[0].value.i32=3;
    inputs[0]=NULL;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    inputs[0]=&in;outputs[0]=NULL;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    outputs[0]=&out;in.frame_count=2;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    in.frame_count=3;inputs[1]=&err;err.frame_count=2;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    err.frame_count=3;
    void* aliases[]={ref,ref+1,error,error+1,&state,state.algorithm.w,state.algorithm.x,
        state.algorithm.pos};
    for (unsigned i=0;i<sizeof(aliases)/sizeof(aliases[0]);++i) {
        out.data=aliases[i];
        CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    }
    out.data=output;
    for (unsigned input=0;input<2;++input) {
        OrpheusBuffer* buffer=input ? &err : &in;
        for (unsigned i=4;i<sizeof(aliases)/sizeof(aliases[0]);++i) {
            buffer->data=aliases[i];
            CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
        }
        buffer->data=input ? error : ref;
    }
    CHECK(result.calls==3 && !memcmp(&snapshot,&state,sizeof(state)) &&
        !memcmp(saved_w,state.algorithm.w,sizeof(saved_w)) &&
        !memcmp(saved_x,state.algorithm.x,sizeof(saved_x)) &&
        !memcmp(saved_pos,state.algorithm.pos,sizeof(saved_pos)));
    ctx.frame_count=0;inputs[0]=NULL;inputs[1]=NULL;outputs[0]=NULL;
    CHECK(api->process(handle,&ctx)==0 && result.calls==3 &&
        !memcmp(saved_w,state.algorithm.w,sizeof(saved_w)));
    CHECK(api->reset(handle)==0 && state.channels==2 && state.filter_length==3 &&
        state.step_size==.5f && state.leakage==.75f && state.eps==.125f);
    memset(saved_w,0,sizeof(saved_w));memset(saved_pos,0,sizeof(saved_pos));
    CHECK(!memcmp(saved_w,state.algorithm.w,sizeof(saved_w)) &&
        !memcmp(saved_w,state.algorithm.x,sizeof(saved_w)) &&
        !memcmp(saved_pos,state.algorithm.pos,sizeof(saved_pos)));
    return api->destroy(handle);
}

int main(void) {
    CHECK(allocation_failures()==0);
    CHECK(numerical_and_errors()==0 && live==0);
    return 0;
}
