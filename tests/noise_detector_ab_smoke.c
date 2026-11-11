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
typedef orpheus_builtin_noise_detector_ab_State State;
static unsigned allocations,live,fail_at,formats;

void* noise_detector_ab_test_calloc(size_t count, size_t size) {
    if (++allocations==fail_at) return NULL;
    void* ptr=calloc(count,size);
    if (ptr) ++live;
    return ptr;
}

void noise_detector_ab_test_free(void* ptr) {
    if (ptr) --live;
    free(ptr);
}

int noise_detector_ab_test_snprintf(char* buffer, size_t size, const char* format, ...) {
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
    for (unsigned failure=1;failure<=7;++failure) {
        State state;
        OrpheusConfig config={.channels=2,.block_size=7,.param_count=1,.param_ids=&key,
            .param_values=&value,.state_block=&state};
        void* handle=NULL;
        CHECK(api->create(&handle,&config)==0 && handle==&state);
        allocations=0;fail_at=failure;
        CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_OUT_OF_MEMORY && live==0 &&
            !state.algorithm.sxx && !state.algorithm.syy && !state.algorithm.sxy_re && !state.algorithm.sxy_im &&
            !state.algorithm.win && !state.algorithm.re && !state.algorithm.im);
        fail_at=0;
        CHECK(api->prepare(handle,&config)==0 && live==7);
        CHECK(api->destroy(handle)==0 && api->destroy(handle)==0 && live==0);
    }
    return 0;
}

typedef struct { State* state; unsigned calls; int error; } Delivery;
static int emit(void* user, uint32_t port, const OrpheusDelivery* delivery) {
    Delivery* result=user;
    float ratio=(float)((double)result->state->algorithm.noise_windows/result->state->algorithm.total_windows);
    if (port!=1 || !delivery || !delivery->buffer || delivery->frame_index!=333 ||
        delivery->epoch!=8 || delivery->buffer->frame_count!=16 ||
        result->state->noise_frames<0 || result->state->clicks<0 ||
        result->state->noise_ratio!=ratio)
        return ORPHEUS_ERR_INVALID_ARG;
    ++result->calls;
    return result->error;
}

static void dft(const float* data, unsigned window, double re[2][4], double im[2][4]) {
    for (unsigned c=0;c<2;++c) for (unsigned k=0;k<4;++k) {
        re[c][k]=0;im[c][k]=0;
        for (unsigned f=0;f<8;++f) {
            double win=.5-.5*cos(2.0*3.14159265358979323846*f/7.0);
            double angle=-2.0*3.14159265358979323846*k*f/8.0;
            double value=(double)data[(window*8+f)*2+c]*win;
            re[c][k]+=value*cos(angle);im[c][k]+=value*sin(angle);
        }
    }
}

static double coherence(double xx, double yy, double re, double im) {
    return xx*yy>1e-12 ? fmin(1.0,(re*re+im*im)/(xx*yy)) : 0;
}

static double thd(double residue, double power) {
    return power>1e-12 ? 10.0*log10(fmax(1e-12,residue/power)) : -120;
}

static int numerical_and_errors(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    State state;
    const char* ids[]={"fft_size","batches","smoothing"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=8},
        {.type=ORPHEUS_VALUE_INT,.value.i32=2},{.type=ORPHEUS_VALUE_INT,.value.i32=3}};
    OrpheusConfig config={.channels=2,.block_size=7,.param_count=3,.param_ids=ids,.param_values=params,
        .state_block=&state};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0 && live==7);
    float ref[33],input[33],output[33];
    OrpheusBuffer br={.data=ref,.channels=2,.format=ORPHEUS_FORMAT_F32,.interleaved=true,
        .frame_count=16,.frame_capacity=16},in=br,out=br;
    in.data=input;out.data=output;
    const OrpheusBuffer* inputs[]={&br,&in};OrpheusBuffer* outputs[]={&out};
    Delivery result={.state=&state};
    OrpheusContinuation continuation={.user=&result,.emit=emit};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=2,.output_count=1,
        .frame_count=7,.valid_frames=7,.frame_index=333,.epoch=8,.trigger_input=1,.continuation=&continuation};
    double xx[8]={0},yy[8]={0},xy_re[8]={0},xy_im[8]={0};
    uint64_t noise_windows=0,noisy=0,clicks=0;
    float peak=0;
    for (unsigned call=0;call<8;++call) {
        for (unsigned i=0;i<32;++i) {
            ref[i]=(float)((int)((i*7+call*11)%37)-18)*.0625f;
            input[i]=call==0 ? ref[i] : .5f*ref[i]+(float)((int)((i*5+call*3)%23)-11)*.03125f;
        }
        result.error=call==0 ? -77 : 0;
        unsigned before=formats;
        CHECK(api->process(handle,&ctx)==result.error && result.calls==call+1 && formats==before &&
            !memcmp(output,input,32*sizeof(float)));
        double accumulated_power=0,accumulated_residue=0;
        for (unsigned window=0;window<2;++window) {
            double xr[2][4],xi[2][4],yr[2][4],yi[2][4];
            dft(ref,window,xr,xi);dft(input,window,yr,yi);
            for (unsigned c=0;c<2;++c) for (unsigned k=0;k<4;++k) {
                unsigned index=c*4+k;
                double a=xr[c][k]*xr[c][k]+xi[c][k]*xi[c][k];
                double b=yr[c][k]*yr[c][k]+yi[c][k]*yi[c][k];
                double r=xr[c][k]*yr[c][k]+xi[c][k]*yi[c][k];
                double v=xi[c][k]*yr[c][k]-xr[c][k]*yi[c][k];
                xx[index]+=(a-xx[index])/3.0;yy[index]+=(b-yy[index])/3.0;
                xy_re[index]+=(r-xy_re[index])/3.0;xy_im[index]+=(v-xy_im[index])/3.0;
            }
            double power=0,residue=0;
            for (unsigned c=0;c<2;++c) for (unsigned k=1;k<4;++k) {
                unsigned i=c*4+k;
                power+=yy[i];residue+=(1-coherence(xx[i],yy[i],xy_re[i],xy_im[i]))*yy[i];
            }
            noise_windows+=thd(residue,power)>-40;
            accumulated_power+=power;accumulated_residue+=residue;
        }
        double reference_thd=thd(accumulated_residue,accumulated_power);
        CHECK(fabs((double)state.thd_n_db-reference_thd)<.002);
        for (unsigned i=0;i<8;++i) {
            CHECK(fabs(state.algorithm.sxx[i]-xx[i])<1e-5*(1+fabs(xx[i])) &&
                fabs(state.algorithm.syy[i]-yy[i])<1e-5*(1+fabs(yy[i])) &&
                fabs(state.algorithm.sxy_re[i]-xy_re[i])<1e-5*(1+fabs(xy_re[i])) &&
                fabs(state.algorithm.sxy_im[i]-xy_im[i])<1e-5*(1+fabs(xy_im[i])));
        }
        double middle=0,high=0;
        for (unsigned c=0;c<2;++c) for (unsigned k=1;k<4;++k) {
            unsigned i=c*4+k;
            double gamma=coherence(xx[i],yy[i],xy_re[i],xy_im[i]);
            if (k<3) middle+=gamma;else high+=gamma;
        }
        CHECK(state.algorithm.coherence[0]==0 &&
            fabs(state.algorithm.coherence[1]-middle/4)<1e-5 &&
            fabs(state.algorithm.coherence[2]-high/2)<1e-5);
        for (unsigned i=0;i<32;++i) {
            float residue=fabsf(ref[i]-input[i]);
            noisy+=residue>state.time_thres;clicks+=residue>.2f;
            if (residue>peak) peak=residue;
        }
        float ratio=(float)((double)noise_windows/((call+1)*2));
        CHECK(state.algorithm.noise_windows==noise_windows && state.noise_frames==(int32_t)noise_windows &&
            state.algorithm.noisy_samples==noisy && state.algorithm.clicks_total==clicks &&
            state.algorithm.total_windows==(call+1)*2 && state.algorithm.total_samples==(call+1)*32 &&
            !memcmp(&peak,&state.residue_pk,sizeof(float)) && !memcmp(&ratio,&state.noise_ratio,sizeof(float)));
        OrpheusValue detail={0};
        CHECK(api->get_parameter(handle,"detail",&detail)==0 && formats==before+1 &&
            strstr(detail.value.str,"\"total_samples\":"));
    }
    State snapshot=state;
    float saved[72];
    memcpy(saved,state.algorithm.sxx,8*sizeof(float));memcpy(saved+8,state.algorithm.syy,8*sizeof(float));
    memcpy(saved+16,state.algorithm.sxy_re,8*sizeof(float));memcpy(saved+24,state.algorithm.sxy_im,8*sizeof(float));
    memcpy(saved+32,state.algorithm.win,8*sizeof(float));memcpy(saved+40,state.algorithm.re,16*sizeof(float));
    memcpy(saved+56,state.algorithm.im,16*sizeof(float));
    params[0].value.i32=10;
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG && !memcmp(&snapshot,&state,sizeof(state)) && live==7);
    params[0].value.i32=8;
    ctx.trigger_input=0;CHECK(api->process(handle,&ctx)!=0);ctx.trigger_input=1;
    OrpheusBuffer* buffers[]={&br,&in};
    float* samples[]={ref,input};
    for (unsigned i=0;i<2;++i) {
        inputs[i]=NULL;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);inputs[i]=buffers[i];
        buffers[i]->frame_count=15;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);buffers[i]->frame_count=16;
    }
    out.frame_count=15;out.frame_capacity=15;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    out.frame_count=16;out.frame_capacity=16;
    void* aliases[]={ref,ref+1,input,input+1,&state,state.algorithm.sxx,state.algorithm.syy,
        state.algorithm.sxy_re,state.algorithm.sxy_im,state.algorithm.win,state.algorithm.re,state.algorithm.im};
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
    uint64_t* counters[]={&state.algorithm.total_windows,&state.algorithm.total_samples,&state.algorithm.noise_windows,
        &state.algorithm.noisy_samples,&state.algorithm.clicks_total};
    for (unsigned i=0;i<5;++i) {
        uint64_t before=*counters[i];*counters[i]=UINT64_MAX;
        CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_PROCESSING);*counters[i]=before;
    }
    CHECK(result.calls==8 && !memcmp(&snapshot,&state,sizeof(state)) &&
        !memcmp(saved,state.algorithm.sxx,8*sizeof(float)) && !memcmp(saved+8,state.algorithm.syy,8*sizeof(float)) &&
        !memcmp(saved+16,state.algorithm.sxy_re,8*sizeof(float)) && !memcmp(saved+24,state.algorithm.sxy_im,8*sizeof(float)) &&
        !memcmp(saved+32,state.algorithm.win,8*sizeof(float)) && !memcmp(saved+40,state.algorithm.re,16*sizeof(float)) &&
        !memcmp(saved+56,state.algorithm.im,16*sizeof(float)));
    ctx.frame_count=0;inputs[0]=NULL;inputs[1]=NULL;outputs[0]=NULL;
    CHECK(api->process(handle,&ctx)==0 && !memcmp(&snapshot,&state,sizeof(state)));
    ctx.frame_count=7;inputs[0]=&br;inputs[1]=&in;outputs[0]=&out;
    CHECK(api->reset(handle)==0);
    state.algorithm.total_windows=INT32_MAX-1;state.algorithm.noise_windows=INT32_MAX-1;
    state.algorithm.total_samples=INT32_MAX-1;state.algorithm.noisy_samples=INT32_MAX-1;
    state.algorithm.clicks_total=INT32_MAX-1;
    for (unsigned i=0;i<32;++i) { ref[i]=0;input[i]=1; }
    CHECK(api->process(handle,&ctx)==0 && state.noise_frames==INT32_MAX && state.clicks==INT32_MAX);
    OrpheusValue detail={0};
    CHECK(api->get_parameter(handle,"detail",&detail)==0 &&
        strstr(detail.value.str,"\"noise_frames\":2147483648,") && strstr(detail.value.str,"\"clicks\":2147483678,"));
    unsigned calls=result.calls;
    uint64_t windows=state.algorithm.total_windows;
    outputs[0]=NULL;
    CHECK(api->process(handle,&ctx)==0 && result.calls==calls && state.algorithm.total_windows==windows+2);
    state.algorithm.coherence[0]=NAN;
    CHECK(api->get_parameter(handle,"detail",&detail)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->reset(handle)==0 && state.fft_size==8 && state.batches==2 && state.channels==2 &&
        state.residue_pk==0 && state.thd_n_db==0 && state.noise_frames==0 && state.clicks==0 && state.noise_ratio==0);
    CHECK(api->get_parameter(handle,"detail",&detail)==0 &&
        !strcmp(detail.value.str,"{\"thd_n_db\":0.00,\"coh_lo\":0.000,\"coh_mid\":0.000,\"coh_hi\":0.000,"
            "\"noise_frames\":0,\"total_frames\":0,\"noisy_samples\":0,\"total_samples\":0,\"clicks\":0,\"residue_pk\":0.0000}"));
    return api->destroy(handle);
}

int main(void) {
    CHECK(allocation_failures()==0);
    CHECK(numerical_and_errors()==0 && live==0);
    return 0;
}
