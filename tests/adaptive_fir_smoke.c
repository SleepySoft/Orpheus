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
typedef orpheus_builtin_adaptive_fir_State State;
static unsigned allocations,live,fail_at,formats;

void* adaptive_fir_test_calloc(size_t count, size_t size) {
    if (++allocations==fail_at) return NULL;
    void* ptr=calloc(count,size);
    if (ptr) ++live;
    return ptr;
}

void adaptive_fir_test_free(void* ptr) {
    if (ptr) --live;
    free(ptr);
}

int adaptive_fir_test_snprintf(char* buffer, size_t size, const char* format, ...) {
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
            !state.algorithm.w && !state.algorithm.xbuf && !state.algorithm.dbuf && !state.algorithm.pos);
        fail_at=0;
        CHECK(api->prepare(handle,&config)==0 && live==4);
        CHECK(api->destroy(handle)==0 && api->destroy(handle)==0 && live==0);
    }
    return 0;
}

typedef struct { State* state; unsigned calls; int error; } Delivery;
static int emit(void* user, uint32_t port, const OrpheusDelivery* delivery) {
    Delivery* result=user;
    if (port!=1 || !delivery || !delivery->buffer || delivery->frame_index!=333 ||
        delivery->epoch!=8 || delivery->buffer->frame_count!=3 ||
        result->state->algorithm.total_frames!=result->calls+1 ||
        !isfinite(result->state->conv_metric))
        return ORPHEUS_ERR_INVALID_ARG;
    ++result->calls;
    return result->error;
}

static int numerical_and_errors(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    State state;
    const char* ids[]={"filter_length","step_size","leakage","eps","secondary_gain"};
    OrpheusValue params[]={
        {.type=ORPHEUS_VALUE_INT,.value.i32=3},
        {.type=ORPHEUS_VALUE_FLOAT,.value.f32=.5f},
        {.type=ORPHEUS_VALUE_FLOAT,.value.f32=.75f},
        {.type=ORPHEUS_VALUE_FLOAT,.value.f32=.125f},
        {.type=ORPHEUS_VALUE_FLOAT,.value.f32=1.5f}};
    OrpheusConfig config={.channels=2,.param_count=5,.param_ids=ids,.param_values=params,
        .state_block=&state};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0 && live==4);
    OrpheusValue detail={0};
    CHECK(api->get_parameter(handle,"detail",&detail)==0 &&
        !strcmp(detail.value.str,"{\"filter_length\":3,\"conv_metric\":0}"));
    float x[7],deriv[7],error[7],output[7],weights[2][3]={{0}},history[2][3]={{0}},dhistory[2][3]={{0}};
    OrpheusBuffer bx={.data=x,.channels=2,.format=ORPHEUS_FORMAT_F32,.interleaved=true,
        .frame_count=3,.frame_capacity=3},bd=bx,be=bx,out=bx;
    bd.data=deriv;be.data=error;out.data=output;
    const OrpheusBuffer* inputs[]={&bx,&bd,&be};OrpheusBuffer* outputs[]={&out};
    Delivery result={.state=&state};
    OrpheusContinuation continuation={.user=&result,.emit=emit};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=3,.output_count=1,
        .frame_count=3,.valid_frames=3,.frame_index=333,.epoch=8,.continuation=&continuation};
    for (unsigned pass=0;pass<3;++pass) {
        for (unsigned i=0;i<6;++i) {
            x[i]=(float)((int)((i+pass*6)*7%17)-8)*.125f;
            deriv[i]=(float)((int)((i+pass*6)*5%23)-11)*.0625f;
            error[i]=(float)((int)((i+pass*6)*3%11)-5)*.25f;
        }
        result.error=pass==0 ? -77 : 0;
        unsigned before=formats;
        CHECK(api->process(handle,&ctx)==result.error && result.calls==pass+1 && formats==before);
        double energy=0;
        for (unsigned c=0;c<2;++c) for (unsigned n=0;n<3;++n) {
            for (unsigned k=2;k>0;--k) {
                history[c][k]=history[c][k-1];dhistory[c][k]=dhistory[c][k-1];
            }
            history[c][0]=x[n*2+c];dhistory[c][0]=deriv[n*2+c];
            float value=0,norm=0;
            for (unsigned k=0;k<3;++k) {
                value+=weights[c][k]*history[c][k];
                norm+=dhistory[c][k]*dhistory[c][k];
            }
            CHECK(!memcmp(output+n*2+c,&value,sizeof(float)));
            float e=error[n*2+c]-1.5f*value;
            float scale=.5f*e/(norm+.125f);
            for (unsigned k=0;k<3;++k)
                weights[c][k]=.75f*weights[c][k]+scale*dhistory[c][k];
            energy+=(double)(e*e);
        }
        float metric=(float)sqrt(energy/(6.0+1e-12));
        CHECK(!memcmp(state.algorithm.w,weights,sizeof(weights)) &&
            !memcmp(&metric,&state.conv_metric,sizeof(float)));
        char expected[128];
        snprintf(expected,sizeof(expected),"{\"filter_length\":3,\"conv_metric\":%.6g}",(double)metric);
        CHECK(api->get_parameter(handle,"detail",&detail)==0 && formats==before+1 &&
            !strcmp(detail.value.str,expected));
    }
    State snapshot=state;
    float saved_w[6],saved_x[6],saved_d[6];uint32_t saved_pos[2];
    memcpy(saved_w,state.algorithm.w,sizeof(saved_w));
    memcpy(saved_x,state.algorithm.xbuf,sizeof(saved_x));
    memcpy(saved_d,state.algorithm.dbuf,sizeof(saved_d));
    memcpy(saved_pos,state.algorithm.pos,sizeof(saved_pos));
    params[0].value.i32=1025;
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG &&
        !memcmp(&snapshot,&state,sizeof(state)) && live==4);
    params[0].value.i32=3;
    OrpheusBuffer* buffers[]={&bx,&bd,&be};
    float* samples[]={x,deriv,error};
    for (unsigned i=0;i<3;++i) {
        inputs[i]=NULL;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);inputs[i]=buffers[i];
        buffers[i]->frame_count=2;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
        buffers[i]->frame_count=3;
    }
    outputs[0]=NULL;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);outputs[0]=&out;
    out.frame_count=2;out.frame_capacity=2;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    out.frame_count=3;out.frame_capacity=3;
    void* aliases[]={x,x+1,deriv,deriv+1,error,error+1,&state,state.algorithm.w,
        state.algorithm.xbuf,state.algorithm.dbuf,state.algorithm.pos};
    for (unsigned i=0;i<sizeof(aliases)/sizeof(aliases[0]);++i) {
        out.data=aliases[i];CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    }
    out.data=output;
    for (unsigned i=0;i<3;++i) {
        for (unsigned j=6;j<sizeof(aliases)/sizeof(aliases[0]);++j) {
            buffers[i]->data=aliases[j];CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
        }
        buffers[i]->data=samples[i];
    }
    CHECK(result.calls==3 && !memcmp(&snapshot,&state,sizeof(state)) &&
        !memcmp(saved_w,state.algorithm.w,sizeof(saved_w)) &&
        !memcmp(saved_x,state.algorithm.xbuf,sizeof(saved_x)) &&
        !memcmp(saved_d,state.algorithm.dbuf,sizeof(saved_d)) &&
        !memcmp(saved_pos,state.algorithm.pos,sizeof(saved_pos)));
    ctx.frame_count=0;inputs[0]=NULL;inputs[1]=NULL;inputs[2]=NULL;outputs[0]=NULL;
    CHECK(api->process(handle,&ctx)==0 && result.calls==3 && !memcmp(&snapshot,&state,sizeof(state)));
    state.conv_metric=NAN;
    CHECK(api->get_parameter(handle,"detail",&detail)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->reset(handle)==0 && state.channels==2 && state.filter_length==3 &&
        state.secondary_gain==1.5f && state.algorithm.total_frames==0 && state.conv_metric==0);
    memset(saved_w,0,sizeof(saved_w));memset(saved_pos,0,sizeof(saved_pos));
    CHECK(!memcmp(saved_w,state.algorithm.w,sizeof(saved_w)) &&
        !memcmp(saved_w,state.algorithm.xbuf,sizeof(saved_w)) &&
        !memcmp(saved_w,state.algorithm.dbuf,sizeof(saved_w)) &&
        !memcmp(saved_pos,state.algorithm.pos,sizeof(saved_pos)));
    CHECK(api->get_parameter(handle,"detail",&detail)==0 &&
        !strcmp(detail.value.str,"{\"filter_length\":3,\"conv_metric\":0}"));
    return api->destroy(handle);
}

int main(void) {
    CHECK(allocation_failures()==0);
    CHECK(numerical_and_errors()==0 && live==0);
    return 0;
}
