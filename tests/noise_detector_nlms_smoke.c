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
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%d: %s\n", __LINE__, #expr); return 1; } } while (0)
typedef orpheus_builtin_noise_detector_nlms_State State;
static unsigned allocations,live,fail_at,formats;

void* noise_detector_nlms_test_calloc(size_t count, size_t size) {
    if (++allocations==fail_at) return NULL;
    void* ptr=calloc(count,size);
    if (ptr) ++live;
    return ptr;
}

void noise_detector_nlms_test_free(void* ptr) {
    if (ptr) --live;
    free(ptr);
}

int noise_detector_nlms_test_snprintf(char* buffer, size_t size, const char* format, ...) {
    va_list args;
    va_start(args,format);
    ++formats;
    int result=vsnprintf(buffer,size,format,args);
    va_end(args);
    return result;
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
        CHECK(api->destroy(handle)==0 && api->destroy(handle)==0 && live==0);
    }
    return 0;
}

typedef struct { State* state; unsigned calls,frames; int error; } Delivery;
static int emit(void* user, uint32_t port, const OrpheusDelivery* delivery) {
    Delivery* result=user;
    float ratio=(float)((double)result->state->algorithm.noise_calls/result->state->algorithm.total_calls);
    if (port!=1 || !delivery || !delivery->buffer || delivery->frame_index!=333 ||
        delivery->epoch!=8 || delivery->buffer->frame_count!=result->frames ||
        result->state->noise_frames<0 || result->state->clicks<0 || result->state->noise_ratio!=ratio)
        return ORPHEUS_ERR_INVALID_ARG;
    ++result->calls;
    return result->error;
}

static int numerical_and_errors(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    State state;
    const char* ids[]={"filter_length","step_size","leakage","eps","time_thres"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=3},
        {.type=ORPHEUS_VALUE_FLOAT,.value.f32=.5f},{.type=ORPHEUS_VALUE_FLOAT,.value.f32=.75f},
        {.type=ORPHEUS_VALUE_FLOAT,.value.f32=.125f},{.type=ORPHEUS_VALUE_FLOAT,.value.f32=.25f}};
    OrpheusConfig config={.channels=2,.param_count=5,.param_ids=ids,.param_values=params,.state_block=&state};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0 && live==3);
    float ref[7],input[7],output[7],weights[2][3]={{0}},history[2][3]={{0}};
    OrpheusBuffer br={.data=ref,.channels=2,.format=ORPHEUS_FORMAT_F32,.interleaved=true,
        .frame_count=3,.frame_capacity=3},in=br,out=br;
    in.data=input;out.data=output;
    const OrpheusBuffer* inputs[]={&br,&in};OrpheusBuffer* outputs[]={&out};
    Delivery result={.state=&state};
    OrpheusContinuation continuation={.user=&result,.emit=emit};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=2,.output_count=1,
        .frame_count=3,.valid_frames=3,.frame_index=333,.epoch=8,.trigger_input=1,.continuation=&continuation};
    double accumulated_input=0,accumulated_residue=0;
    uint64_t noise_calls=0,noisy=0,clicks=0,total_samples=0;
    float peak=0;
    const unsigned lengths[]={3,1,2};
    for (unsigned call=0;call<3;++call) {
        unsigned frames=lengths[call];
        result.frames=frames;ctx.frame_count=frames;ctx.valid_frames=frames;br.frame_count=frames;in.frame_count=frames;
        for (unsigned i=0;i<frames*2;++i) {
            ref[i]=(float)((int)((i+call*6)*7%17)-8)*.125f;
            input[i]=(float)((int)((i+call*6)*3%11)-5)*.25f;
        }
        result.error=call==0 ? -77 : 0;
        unsigned before=formats;
        CHECK(api->process(handle,&ctx)==result.error && result.calls==call+1 && formats==before &&
            !memcmp(output,input,frames*2*sizeof(float)));
        double input_energy=0,residue_energy=0;
        for (unsigned c=0;c<2;++c) for (unsigned n=0;n<frames;++n) {
            for (unsigned k=2;k>0;--k) history[c][k]=history[c][k-1];
            history[c][0]=ref[n*2+c];
            float value=0,norm=0;
            for (unsigned k=0;k<3;++k) {
                value+=weights[c][k]*history[c][k];norm+=history[c][k]*history[c][k];
            }
            float error=input[n*2+c]-value,absolute=fabsf(error);
            residue_energy+=(double)error*error;input_energy+=(double)input[n*2+c]*input[n*2+c];
            noisy+=absolute>.25f;clicks+=absolute>.2f;
            if (absolute>peak) peak=absolute;
            float scale=.5f*error/(norm+.125f);
            for (unsigned k=0;k<3;++k) weights[c][k]=.75f*weights[c][k]+scale*history[c][k];
        }
        double db=10.0*log10((residue_energy+1e-12)/(input_energy+1e-12));
        float residue_db=(float)db;
        noise_calls+=db>-25;
        accumulated_input+=input_energy;accumulated_residue+=residue_energy;total_samples+=frames*2;
        float erle=10.0f*log10f((float)((accumulated_input+1e-12)/(accumulated_residue+1e-12)));
        float ratio=(float)((double)noise_calls/(call+1));
        CHECK(!memcmp(state.algorithm.w,weights,sizeof(weights)) &&
            !memcmp(&residue_db,&state.residue_db,sizeof(float)) && !memcmp(&erle,&state.erle_db,sizeof(float)) &&
            !memcmp(&ratio,&state.noise_ratio,sizeof(float)) && !memcmp(&peak,&state.residue_pk,sizeof(float)) &&
            state.algorithm.accumulated_input==accumulated_input && state.algorithm.accumulated_residue==accumulated_residue &&
            state.algorithm.noise_calls==noise_calls && state.algorithm.noisy_samples==noisy &&
            state.algorithm.clicks_total==clicks && state.algorithm.total_samples==total_samples);
        OrpheusValue detail={0};
        char expected[512];
        snprintf(expected,sizeof(expected),
            "{\"residue_db\":%.2f,\"erle_db\":%.2f,\"noise_frames\":%llu,\"total_frames\":%u,"
            "\"noisy_samples\":%llu,\"total_samples\":%llu,\"clicks\":%llu,\"residue_pk\":%.4f}",
            (double)residue_db,(double)erle,(unsigned long long)noise_calls,call+1,
            (unsigned long long)noisy,(unsigned long long)total_samples,(unsigned long long)clicks,(double)peak);
        CHECK(api->get_parameter(handle,"detail",&detail)==0 && formats==before+1 && !strcmp(detail.value.str,expected));
    }
    State snapshot=state;
    float saved_w[6],saved_x[6];uint32_t saved_pos[2];
    memcpy(saved_w,state.algorithm.w,sizeof(saved_w));memcpy(saved_x,state.algorithm.x,sizeof(saved_x));
    memcpy(saved_pos,state.algorithm.pos,sizeof(saved_pos));
    params[0].value.i32=4097;
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&snapshot,&state,sizeof(state)) && live==3);
    params[0].value.i32=3;
    ctx.trigger_input=0;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);ctx.trigger_input=1;
    OrpheusBuffer* buffers[]={&br,&in};
    float* samples[]={ref,input};
    for (unsigned i=0;i<2;++i) {
        inputs[i]=NULL;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);inputs[i]=buffers[i];
        buffers[i]->frame_count=1;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);buffers[i]->frame_count=2;
    }
    out.frame_count=1;out.frame_capacity=1;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    out.frame_count=2;out.frame_capacity=3;
    void* aliases[]={ref,ref+1,input,input+1,&state,state.algorithm.w,state.algorithm.x,state.algorithm.pos};
    for (unsigned i=0;i<sizeof(aliases)/sizeof(aliases[0]);++i) {
        out.data=aliases[i];CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    }
    out.data=output;
    for (unsigned input_index=0;input_index<2;++input_index) {
        for (unsigned i=4;i<sizeof(aliases)/sizeof(aliases[0]);++i) {
            buffers[input_index]->data=aliases[i];CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
        }
        buffers[input_index]->data=samples[input_index];
    }
    uint64_t* counters[]={&state.algorithm.total_calls,&state.algorithm.total_samples,&state.algorithm.noise_calls,
        &state.algorithm.noisy_samples,&state.algorithm.clicks_total};
    for (unsigned i=0;i<5;++i) {
        uint64_t before=*counters[i];*counters[i]=UINT64_MAX;
        CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_PROCESSING);*counters[i]=before;
    }
    CHECK(result.calls==3 && !memcmp(&snapshot,&state,sizeof(state)) &&
        !memcmp(saved_w,state.algorithm.w,sizeof(saved_w)) && !memcmp(saved_x,state.algorithm.x,sizeof(saved_x)) &&
        !memcmp(saved_pos,state.algorithm.pos,sizeof(saved_pos)));
    ctx.frame_count=0;inputs[0]=NULL;inputs[1]=NULL;outputs[0]=NULL;
    CHECK(api->process(handle,&ctx)==0 && !memcmp(&snapshot,&state,sizeof(state)));
    ctx.frame_count=2;inputs[0]=&br;inputs[1]=&in;outputs[0]=&out;
    CHECK(api->reset(handle)==0);
    state.algorithm.total_calls=INT32_MAX;state.algorithm.noise_calls=INT32_MAX;
    state.algorithm.total_samples=(uint64_t)INT32_MAX*2;state.algorithm.clicks_total=INT32_MAX;
    for (unsigned i=0;i<4;++i) { ref[i]=0;input[i]=1; }
    CHECK(api->process(handle,&ctx)==0 && state.noise_frames==INT32_MAX && state.clicks==INT32_MAX);
    OrpheusValue detail={0};
    CHECK(api->get_parameter(handle,"detail",&detail)==0 &&
        strstr(detail.value.str,"\"noise_frames\":2147483648,") && strstr(detail.value.str,"\"clicks\":2147483651,"));
    unsigned calls=result.calls;
    uint64_t total=state.algorithm.total_calls;
    outputs[0]=NULL;
    CHECK(api->process(handle,&ctx)==0 && result.calls==calls && state.algorithm.total_calls==total+1);
    state.erle_db=NAN;
    CHECK(api->get_parameter(handle,"detail",&detail)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->reset(handle)==0 && state.channels==2 && state.filter_length==3 &&
        state.residue_pk==0 && state.residue_db==0 && state.erle_db==0 &&
        state.noise_ratio==0 && state.noise_frames==0 && state.clicks==0);
    memset(saved_w,0,sizeof(saved_w));memset(saved_pos,0,sizeof(saved_pos));
    CHECK(!memcmp(saved_w,state.algorithm.w,sizeof(saved_w)) && !memcmp(saved_w,state.algorithm.x,sizeof(saved_w)) &&
        !memcmp(saved_pos,state.algorithm.pos,sizeof(saved_pos)));
    CHECK(api->get_parameter(handle,"detail",&detail)==0 &&
        !strcmp(detail.value.str,"{\"residue_db\":0.00,\"erle_db\":0.00,\"noise_frames\":0,\"total_frames\":0,"
            "\"noisy_samples\":0,\"total_samples\":0,\"clicks\":0,\"residue_pk\":0.0000}"));
    return api->destroy(handle);
}

int main(void) {
    CHECK(allocation_failures()==0);
    CHECK(numerical_and_errors()==0 && live==0);
    return 0;
}
