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
typedef orpheus_builtin_noise_detector_State State;
static unsigned allocations,live,fail_at,formats;

void* noise_detector_test_calloc(size_t count, size_t size) {
    if (++allocations==fail_at) return NULL;
    void* ptr=calloc(count,size);
    if (ptr) ++live;
    return ptr;
}

void noise_detector_test_free(void* ptr) {
    if (ptr) --live;
    free(ptr);
}

int noise_detector_test_snprintf(char* buffer, size_t size, const char* format, ...) {
    va_list args;
    va_start(args,format);
    ++formats;
    int result=vsnprintf(buffer,size,format,args);
    va_end(args);
    return result;
}

static int allocation_failures(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    const char* key="fft_size";
    OrpheusValue value={.type=ORPHEUS_VALUE_INT,.value.i32=4};
    for (unsigned failure=1;failure<=3;++failure) {
        State state;
        OrpheusConfig config={.channels=2,.block_size=7,.param_count=1,.param_ids=&key,
            .param_values=&value,.state_block=&state};
        void* handle=NULL;
        CHECK(api->create(&handle,&config)==0 && handle==&state);
        allocations=0;fail_at=failure;
        CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_OUT_OF_MEMORY && live==0 &&
            !state.algorithm.win && !state.algorithm.re && !state.algorithm.im);
        fail_at=0;
        CHECK(api->prepare(handle,&config)==0 && live==3);
        CHECK(api->destroy(handle)==0 && api->destroy(handle)==0 && live==0);
    }
    return 0;
}

typedef struct { State* state; unsigned calls; int error; } Delivery;
static int emit(void* user, uint32_t port, const OrpheusDelivery* delivery) {
    Delivery* result=user;
    if (port!=1 || !delivery || !delivery->buffer || delivery->frame_index!=333 ||
        delivery->epoch!=8 || delivery->buffer->frame_count!=16 ||
        result->state->algorithm.total_samples!=(uint64_t)(result->calls+1)*48 ||
        result->state->clicks<0)
        return ORPHEUS_ERR_INVALID_ARG;
    ++result->calls;
    return result->error;
}

static double reference_flatness(const float* input) {
    double flat=0;
    for (unsigned c=0;c<3;++c) {
        double sum=0,logs=0;
        for (unsigned w=0;w<2;++w) for (unsigned bin=1;bin<4;++bin) {
            double re=0,im=0;
            for (unsigned f=0;f<8;++f) {
                double window=.5-.5*cos(2.0*3.14159265358979323846*f/7.0);
                double angle=-2.0*3.14159265358979323846*bin*f/8.0;
                double sample=(double)input[(w*8+f)*3+c]*window;
                re+=sample*cos(angle);im+=sample*sin(angle);
            }
            double p=re*re+im*im+(double)1e-12f;
            sum+=p;logs+=log(p);
        }
        float arithmetic=(float)(sum/6.0);
        if (arithmetic>1e-12f) flat+=exp(logs/6.0)/(double)arithmetic;
    }
    return flat/3.0;
}

static int numerical_and_errors(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    State state;
    const char* ids[]={"fft_size","batches","clip_level","click_thres"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=8},
        {.type=ORPHEUS_VALUE_INT,.value.i32=2},
        {.type=ORPHEUS_VALUE_FLOAT,.value.f32=.5f},
        {.type=ORPHEUS_VALUE_FLOAT,.value.f32=.3f}};
    OrpheusConfig config={.channels=3,.block_size=7,.param_count=4,.param_ids=ids,.param_values=params,
        .state_block=&state};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0 && live==3);
    float input[49],output[49];
    OrpheusBuffer in={.data=input,.channels=3,.format=ORPHEUS_FORMAT_F32,.interleaved=true,
        .frame_count=16,.frame_capacity=16},out=in;
    out.data=output;
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&out};
    Delivery result={.state=&state};
    OrpheusContinuation continuation={.user=&result,.emit=emit};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,
        .frame_count=7,.valid_frames=7,.frame_index=333,.epoch=8,.continuation=&continuation};
    double ema=0;
    uint64_t clipped=0,clicks=0;
    unsigned tonal=0,noise=0;
    for (unsigned call=0;call<20;++call) {
        const float constant[]={.125f,.875f,-.75f};
        for (unsigned i=0;i<48;++i) {
            if (call==0) input[i]=0;
            else if (call==1) input[i]=constant[i%3];
            else if (call<8) input[i]=(float)((int)((i*7+call*11)%37)-18)*.0625f;
            else input[i]=i/3%8==4 ? (float)((call+i%3)%5+1)*.25f : 0;
        }
        result.error=call==0 ? -77 : 0;
        unsigned before=formats;
        CHECK(api->process(handle,&ctx)==result.error && result.calls==call+1 && formats==before &&
            !memcmp(output,input,48*sizeof(float)));
        double flat=reference_flatness(input);
        ema=ema==0 ? flat : ema*.95+flat*.05;
        CHECK(fabs((double)state.flatness-ema)<1e-5);
        double energy=0,peak=0;
        for (unsigned i=0;i<48;++i) {
            double v=fabs((double)input[i]);energy+=v*v;
            if (v>peak) peak=v;
            if (v>=.5) clipped++;
        }
        uint64_t prior_clicks=clicks;
        for (unsigned f=1;f<16;++f) for (unsigned c=0;c<3;++c)
            if (fabsf(input[f*3+c]-input[(f-1)*3+c])>.3f) clicks++;
        if (call==1) CHECK(prior_clicks==clicks);
        double rmsdb=10.0*log10(energy/49.0+1e-12);
        double peakdb=20.0*log10(peak>1e-9 ? peak : 1e-9);
        double floor=ema>.5 ? rmsdb : (peakdb>rmsdb ? peakdb-10 : rmsdb);
        if (ema>.5) noise++;else tonal++;
        float clip_ratio=(float)((double)clipped/((call+1)*48.0));
        CHECK(fabs((double)state.noise_floor_db-floor)<1e-4 && state.clicks==(int32_t)clicks &&
            state.algorithm.clicks_total==clicks && state.algorithm.clipped_samples==clipped &&
            !memcmp(&clip_ratio,&state.clip_pct,sizeof(float)));
        OrpheusValue detail={0};
        char expected[256];
        snprintf(expected,sizeof(expected),
            "{\"flatness\":%.3f,\"noise_floor_db\":%.2f,\"clicks\":%llu,\"clip_pct\":%.4f}",
            (double)state.flatness,(double)state.noise_floor_db,(unsigned long long)clicks,(double)clip_ratio);
        CHECK(api->get_parameter(handle,"detail",&detail)==0 && formats==before+1 &&
            !strcmp(detail.value.str,expected));
    }
    CHECK(tonal>0 && noise>0);
    State snapshot=state;
    float saved[24];
    memcpy(saved,state.algorithm.win,8*sizeof(float));
    memcpy(saved+8,state.algorithm.re,8*sizeof(float));
    memcpy(saved+16,state.algorithm.im,8*sizeof(float));
    params[0].value.i32=10;
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG &&
        !memcmp(&snapshot,&state,sizeof(state)) && live==3);
    params[0].value.i32=8;
    inputs[0]=NULL;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);inputs[0]=&in;
    outputs[0]=NULL;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);outputs[0]=&out;
    in.frame_count=15;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);in.frame_count=16;
    out.frame_count=15;out.frame_capacity=15;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    out.frame_count=16;out.frame_capacity=16;
    void* aliases[]={input,input+1,&state,state.algorithm.win,state.algorithm.re,state.algorithm.im};
    for (unsigned i=0;i<sizeof(aliases)/sizeof(aliases[0]);++i) {
        out.data=aliases[i];CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    }
    out.data=output;
    for (unsigned i=2;i<sizeof(aliases)/sizeof(aliases[0]);++i) {
        in.data=aliases[i];CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    }
    in.data=input;
    for (unsigned i=0;i<48;++i) input[i]=i%2 ? -1.0f : 1.0f;
    uint64_t* counters[]={&state.algorithm.total_samples,&state.algorithm.clipped_samples,&state.algorithm.clicks_total};
    for (unsigned i=0;i<3;++i) {
        uint64_t before=*counters[i];*counters[i]=UINT64_MAX;
        CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_PROCESSING);
        *counters[i]=before;
    }
    CHECK(result.calls==20 && !memcmp(&snapshot,&state,sizeof(state)) &&
        !memcmp(saved,state.algorithm.win,8*sizeof(float)) &&
        !memcmp(saved+8,state.algorithm.re,8*sizeof(float)) &&
        !memcmp(saved+16,state.algorithm.im,8*sizeof(float)));
    ctx.frame_count=0;inputs[0]=NULL;outputs[0]=NULL;
    CHECK(api->process(handle,&ctx)==0 && !memcmp(&snapshot,&state,sizeof(state)));
    ctx.frame_count=7;inputs[0]=&in;outputs[0]=&out;
    state.algorithm.clicks_total=INT32_MAX-1;
    CHECK(api->process(handle,&ctx)==0 && state.clicks==INT32_MAX &&
        state.algorithm.clicks_total>(uint64_t)INT32_MAX && result.calls==21);
    OrpheusValue detail={0};
    CHECK(api->get_parameter(handle,"clicks",&detail)==0 && detail.value.i32==INT32_MAX);
    CHECK(api->get_parameter(handle,"detail",&detail)==0 && strstr(detail.value.str,"\"clicks\":2147483691,"));
    state.flatness=NAN;
    CHECK(api->get_parameter(handle,"detail",&detail)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->reset(handle)==0 && state.fft_size==8 && state.batches==2 && state.channels==3 &&
        state.algorithm.total_samples==0 && state.algorithm.clicks_total==0 && state.clicks==0);
    CHECK(api->get_parameter(handle,"detail",&detail)==0 &&
        !strcmp(detail.value.str,"{\"flatness\":0.000,\"noise_floor_db\":0.00,\"clicks\":0,\"clip_pct\":0.0000}"));
    return api->destroy(handle);
}

int main(void) {
    CHECK(allocation_failures()==0);
    CHECK(numerical_and_errors()==0 && live==0);
    return 0;
}
