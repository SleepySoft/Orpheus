#include "component_state.h"
#ifdef calloc
#undef calloc
#endif
#ifdef free
#undef free
#endif
#ifdef snprintf
#undef snprintf
#endif
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%d: %s\n", __LINE__, #expr); return 1; } } while (0)
typedef orpheus_builtin_anc_fxlms_State State;
static unsigned allocations,live,fail_at,formats;

void* anc_fxlms_test_calloc(size_t count, size_t size) {
    if (++allocations==fail_at) return NULL;
    void* ptr=calloc(count,size);
    if (ptr) ++live;
    return ptr;
}

void anc_fxlms_test_free(void* ptr) {
    if (ptr) --live;
    free(ptr);
}

int anc_fxlms_test_snprintf(char* buffer, size_t size, const char* format, ...) {
    va_list args;
    va_start(args,format);
    ++formats;
    int result=vsnprintf(buffer,size,format,args);
    va_end(args);
    return result;
}

static int allocation_failures(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    for (unsigned failure=1;failure<=4;++failure) {
        State state;
        OrpheusConfig config={.channels=2,.state_block=&state};
        void* handle=NULL;
        CHECK(api->create(&handle,&config)==0 && handle==&state);
        allocations=0;fail_at=failure;
        CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_OUT_OF_MEMORY && live==0 &&
            !state.algorithm.w && !state.algorithm.xbuf && !state.algorithm.xf && !state.algorithm.pos);
        fail_at=0;
        CHECK(api->prepare(handle,&config)==0 && live==4);
        CHECK(api->destroy(handle)==0 && api->destroy(handle)==0 && live==0);
    }
    return 0;
}

typedef struct { State* state; unsigned calls,frames; int error; } Delivery;
static int emit(void* user, uint32_t port, const OrpheusDelivery* delivery) {
    Delivery* result=user;
    if (port!=1 || !delivery || !delivery->buffer || delivery->frame_index!=333 ||
        delivery->epoch!=8 || delivery->buffer->frame_count!=result->frames ||
        result->state->algorithm.total_frames!=result->calls+1)
        return ORPHEUS_ERR_INVALID_ARG;
    ++result->calls;
    return result->error;
}

static int numerical_and_errors(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    State state;
    const char* ids[]={"filter_length","step_size","leakage","eps","secondary_gain","secondary_delay"};
    OrpheusValue params[]={
        {.type=ORPHEUS_VALUE_INT,.value.i32=3},
        {.type=ORPHEUS_VALUE_FLOAT,.value.f32=.5f},
        {.type=ORPHEUS_VALUE_FLOAT,.value.f32=.75f},
        {.type=ORPHEUS_VALUE_FLOAT,.value.f32=.125f},
        {.type=ORPHEUS_VALUE_FLOAT,.value.f32=1.5f},
        {.type=ORPHEUS_VALUE_FLOAT,.value.f32=4.75f}};
    OrpheusConfig config={.channels=2,.param_count=6,.param_ids=ids,.param_values=params,
        .state_block=&state};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0 && live==4);
    OrpheusValue detail={0};
    const char* empty="{\"noise_reduction_db\":0.00,\"power_d\":0,\"power_e\":0,\"frames\":0}";
    CHECK(api->get_parameter(handle,"detail",&detail)==0 && !strcmp(detail.value.str,empty));
    float x[7],d[7],output[7],weights[2][3]={{0}},history[2][3]={{0}};
    OrpheusBuffer bx={.data=x,.channels=2,.format=ORPHEUS_FORMAT_F32,.interleaved=true,
        .frame_count=3,.frame_capacity=3},bd=bx,out=bx;
    bd.data=d;out.data=output;
    const OrpheusBuffer* inputs[]={&bx,&bd};OrpheusBuffer* outputs[]={&out};
    Delivery result={.state=&state};
    OrpheusContinuation continuation={.user=&result,.emit=emit};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=2,.output_count=1,
        .frame_count=3,.valid_frames=3,.frame_index=333,.epoch=8,.continuation=&continuation};
    double accumulated_d=0,accumulated_e=0;
    unsigned total_samples=0;
    const unsigned lengths[]={3,1,2};
    for (unsigned pass=0;pass<3;++pass) {
        unsigned frames=lengths[pass];
        ctx.frame_count=frames;ctx.valid_frames=frames;result.frames=frames;
        bx.frame_count=frames;bd.frame_count=frames;
        for (unsigned i=0;i<frames*2;++i) {
            x[i]=(float)((int)((i+pass*6)*7%17)-8)*.125f;
            d[i]=(float)((int)((i+pass*6)*3%11)-5)*.25f;
        }
        result.error=pass==0 ? -77 : 0;
        unsigned before=formats;
        CHECK(api->process(handle,&ctx)==result.error && result.calls==pass+1 && formats==before);
        double pd=0,pe=0;
        for (unsigned c=0;c<2;++c) for (unsigned n=0;n<frames;++n) {
            for (unsigned k=2;k>0;--k) history[c][k]=history[c][k-1];
            history[c][0]=x[n*2+c];
            float value=0,norm=0,filtered[3];
            for (unsigned k=0;k<3;++k) {
                filtered[k]=1.5f*history[c][(k+4)%3];
                norm+=filtered[k]*filtered[k];
            }
            for (unsigned k=0;k<3;++k) value+=weights[c][k]*history[c][k];
            float negative=-value;
            CHECK(!memcmp(output+n*2+c,&negative,sizeof(float)));
            float e=d[n*2+c]-1.5f*value;
            float scale=.5f*e/(norm+.125f);
            for (unsigned k=0;k<3;++k)
                weights[c][k]=.75f*weights[c][k]+scale*filtered[k];
            pd+=(double)(d[n*2+c]*d[n*2+c]);pe+=(double)(e*e);
        }
        accumulated_d+=pd;accumulated_e+=pe;total_samples+=frames*2;
        float power_d=(float)(accumulated_d/total_samples),power_e=(float)(accumulated_e/total_samples);
        float nr=power_e>1e-30f ? 10.0f*log10f(power_d/(power_e+1e-30f)) : 0;
        CHECK(!memcmp(state.algorithm.w,weights,sizeof(weights)) &&
            !memcmp(&power_d,&state.power_d,sizeof(float)) &&
            !memcmp(&power_e,&state.power_e,sizeof(float)) &&
            !memcmp(&nr,&state.noise_reduction_db,sizeof(float)) &&
            state.algorithm.total_samples==total_samples);
        char expected[256];
        snprintf(expected,sizeof(expected),
            "{\"noise_reduction_db\":%.2f,\"power_d\":%.6g,\"power_e\":%.6g,\"frames\":%u}",
            (double)nr,(double)power_d,(double)power_e,pass+1);
        int rc=api->get_parameter(handle,"detail",&detail);
        if (rc || formats!=before+1 || strcmp(detail.value.str,expected)) {
            fprintf(stderr,"detail rc=%d formats=%u/%u state_nr=%g expected_nr=%g actual=%s expected=%s\n",
                rc,formats,before+1,(double)state.noise_reduction_db,(double)nr,detail.value.str,expected);
            return 1;
        }
    }
    State snapshot=state;
    float saved_w[6],saved_x[6],saved_f[6];uint32_t saved_pos[2];
    memcpy(saved_w,state.algorithm.w,sizeof(saved_w));
    memcpy(saved_x,state.algorithm.xbuf,sizeof(saved_x));
    memcpy(saved_f,state.algorithm.xf,sizeof(saved_f));
    memcpy(saved_pos,state.algorithm.pos,sizeof(saved_pos));
    params[5].value.f32=513;
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG &&
        !memcmp(&snapshot,&state,sizeof(state)) && live==4);
    params[5].value.f32=4.75f;
    OrpheusBuffer* buffers[]={&bx,&bd};
    float* samples[]={x,d};
    for (unsigned i=0;i<2;++i) {
        inputs[i]=NULL;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);inputs[i]=buffers[i];
        buffers[i]->frame_count=1;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
        buffers[i]->frame_count=2;
    }
    outputs[0]=NULL;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);outputs[0]=&out;
    out.frame_count=1;out.frame_capacity=1;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    out.frame_count=2;out.frame_capacity=3;
    void* aliases[]={x,x+1,d,d+1,&state,state.algorithm.w,
        state.algorithm.xbuf,state.algorithm.xf,state.algorithm.pos};
    for (unsigned i=0;i<sizeof(aliases)/sizeof(aliases[0]);++i) {
        out.data=aliases[i];CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    }
    out.data=output;
    for (unsigned i=0;i<2;++i) {
        for (unsigned j=4;j<sizeof(aliases)/sizeof(aliases[0]);++j) {
            buffers[i]->data=aliases[j];CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
        }
        buffers[i]->data=samples[i];
    }
    state.algorithm.total_samples=UINT64_MAX;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_PROCESSING);
    state.algorithm.total_samples=snapshot.algorithm.total_samples;
    state.algorithm.total_frames=UINT64_MAX;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_PROCESSING);
    state.algorithm.total_frames=snapshot.algorithm.total_frames;
    CHECK(result.calls==3 && !memcmp(&snapshot,&state,sizeof(state)) &&
        !memcmp(saved_w,state.algorithm.w,sizeof(saved_w)) &&
        !memcmp(saved_x,state.algorithm.xbuf,sizeof(saved_x)) &&
        !memcmp(saved_f,state.algorithm.xf,sizeof(saved_f)) &&
        !memcmp(saved_pos,state.algorithm.pos,sizeof(saved_pos)));
    ctx.frame_count=0;inputs[0]=NULL;inputs[1]=NULL;outputs[0]=NULL;
    CHECK(api->process(handle,&ctx)==0 && result.calls==3 && !memcmp(&snapshot,&state,sizeof(state)));
    state.power_d=NAN;
    CHECK(api->get_parameter(handle,"detail",&detail)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->reset(handle)==0 && state.channels==2 && state.filter_length==3 &&
        state.secondary_gain==1.5f && state.secondary_delay==4.75f &&
        state.algorithm.total_frames==0 && state.algorithm.total_samples==0 &&
        state.power_d==0 && state.power_e==0 && state.noise_reduction_db==0);
    memset(saved_w,0,sizeof(saved_w));memset(saved_pos,0,sizeof(saved_pos));
    CHECK(!memcmp(saved_w,state.algorithm.w,sizeof(saved_w)) &&
        !memcmp(saved_w,state.algorithm.xbuf,sizeof(saved_w)) &&
        !memcmp(saved_w,state.algorithm.xf,sizeof(saved_w)) &&
        !memcmp(saved_pos,state.algorithm.pos,sizeof(saved_pos)));
    CHECK(api->get_parameter(handle,"detail",&detail)==0 && !strcmp(detail.value.str,empty));
    return api->destroy(handle);
}

int main(void) {
    CHECK(allocation_failures()==0);
    CHECK(numerical_and_errors()==0 && live==0);
    return 0;
}
