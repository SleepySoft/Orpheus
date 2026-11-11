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
typedef orpheus_builtin_coherence_matrix_State State;
static unsigned allocations,live,fail_at,formats;

void* coherence_matrix_test_calloc(size_t count, size_t size) {
    if (++allocations==fail_at) return NULL;
    void* ptr=calloc(count,size);
    if (ptr) ++live;
    return ptr;
}

void coherence_matrix_test_free(void* ptr) {
    if (ptr) --live;
    free(ptr);
}

int coherence_matrix_test_snprintf(char* buffer, size_t size, const char* format, ...) {
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
    for (unsigned failure=1;failure<=5;++failure) {
        State state;
        OrpheusConfig config={.channels=2,.block_size=7,.param_count=1,.param_ids=&key,
            .param_values=&value,.state_block=&state};
        void* handle=NULL;
        CHECK(api->create(&handle,&config)==0 && handle==&state);
        allocations=0;fail_at=failure;
        CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_OUT_OF_MEMORY && live==0 &&
            !state.algorithm.gxx && !state.algorithm.gxy_re && !state.algorithm.gxy_im &&
            !state.algorithm.re && !state.algorithm.im);
        fail_at=0;
        CHECK(api->prepare(handle,&config)==0 && live==5);
        CHECK(api->destroy(handle)==0 && api->destroy(handle)==0 && live==0);
    }
    return 0;
}

typedef struct { State* state; unsigned calls; int error; } Delivery;
static int emit(void* user, uint32_t port, const OrpheusDelivery* delivery) {
    Delivery* result=user;
    unsigned count=result->calls<64 ? result->calls+1 : 64;
    if (port!=1 || !delivery || !delivery->buffer || delivery->frame_index!=333 ||
        delivery->epoch!=8 || delivery->buffer->frame_count!=8 ||
        result->state->algorithm.hist_count!=count)
        return ORPHEUS_ERR_INVALID_ARG;
    ++result->calls;
    return result->error;
}

static int check_json(const char* cursor, const double* expected, unsigned count, const char* tail) {
    for (unsigned i=0;i<count;++i) {
        char* end;
        float actual=strtof(cursor,&end);
        if (end==cursor || fabs((double)actual-expected[i])>1e-4) return 1;
        if (i+1<count && *end!=',') return 1;
        cursor=end+(i+1<count ? 1 : 0);
    }
    return strcmp(cursor,tail)!=0;
}

static int numerical_and_errors(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    State state;
    const char* ids[]={"fft_size","smoothing"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=8},
        {.type=ORPHEUS_VALUE_INT,.value.i32=3}};
    OrpheusConfig config={.channels=3,.block_size=7,.param_count=2,.param_ids=ids,.param_values=params,
        .state_block=&state};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0 && live==5);
    float input[25],output[25];
    OrpheusBuffer in={.data=input,.channels=3,.format=ORPHEUS_FORMAT_F32,.interleaved=true,
        .frame_count=8,.frame_capacity=8},out=in;
    out.data=output;
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&out};
    Delivery result={.state=&state};
    OrpheusContinuation continuation={.user=&result,.emit=emit};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,
        .frame_count=7,.valid_frames=7,.frame_index=333,.epoch=8,.continuation=&continuation};
    double gxx[12]={0},gre[36]={0},gim[36]={0},matrix[9],history[68];
    for (unsigned call=0;call<68;++call) {
        for (unsigned i=0;i<24;++i) input[i]=(float)((int)((i*7+call*11)%37)-18)*.0625f;
        result.error=call==0 ? -77 : 0;
        out.data=call==67 ? input : output;
        unsigned before=formats;
        CHECK(api->process(handle,&ctx)==result.error && result.calls==call+1 && formats==before &&
            !memcmp(out.data,input,24*sizeof(float)));
        double re[3][4]={{0}},im[3][4]={{0}};
        for (unsigned c=0;c<3;++c) for (unsigned k=0;k<4;++k) for (unsigned f=0;f<8;++f) {
            double angle=-2.0*3.14159265358979323846*k*f/8.0;
            re[c][k]+=(double)input[f*3+c]*cos(angle);
            im[c][k]+=(double)input[f*3+c]*sin(angle);
        }
        for (unsigned k=0;k<4;++k) for (unsigned i=0;i<3;++i) {
            double instant=re[i][k]*re[i][k]+im[i][k]*im[i][k];
            gxx[i*4+k]+=(instant-gxx[i*4+k])/3.0;
            CHECK(fabs(state.algorithm.gxx[i*4+k]-gxx[i*4+k])<1e-5*(1+fabs(gxx[i*4+k])));
            for (unsigned j=0;j<3;++j) {
                unsigned off=(i*3+j)*4+k;
                double r=re[i][k]*re[j][k]+im[i][k]*im[j][k];
                double v=im[i][k]*re[j][k]-re[i][k]*im[j][k];
                gre[off]+=(r-gre[off])/3.0;gim[off]+=(v-gim[off])/3.0;
                CHECK(fabs(state.algorithm.gxy_re[off]-gre[off])<1e-5*(1+fabs(gre[off])) &&
                    fabs(state.algorithm.gxy_im[off]-gim[off])<1e-5*(1+fabs(gim[off])));
            }
        }
        double mean=0;
        for (unsigned i=0;i<3;++i) for (unsigned j=0;j<3;++j) {
            double sum=0;
            for (unsigned k=0;k<4;++k) {
                unsigned off=(i*3+j)*4+k;
                double denom=gxx[i*4+k]*gxx[j*4+k];
                if (denom>1e-12) sum+=(gre[off]*gre[off]+gim[off]*gim[off])/denom;
            }
            matrix[i*3+j]=fmin(1.0,sum/4.0);
            CHECK(fabs(state.algorithm.matrix[i*3+j]-matrix[i*3+j])<1e-5);
            if (i!=j) mean+=matrix[i*3+j];
        }
        history[call]=mean/6.0;
        CHECK(fabs(state.algorithm.hist[call%64]-history[call])<1e-5);
    }
    out.data=output;
    OrpheusValue json={0},hist={0};
    CHECK(api->get_parameter(handle,"coherence",&json)==0);
    const char* start=strstr(json.value.str,"\"matrix\":[");
    CHECK(start && check_json(start+10,matrix,9,"]}")==0);
    CHECK(api->get_parameter(handle,"history",&hist)==0 && hist.value.str[0]=='[' &&
        check_json(hist.value.str+1,history+4,64,"]")==0 &&
        check_json(start+10,matrix,9,"]}")==0);
    State snapshot=state;
    float saved[132];
    memcpy(saved,state.algorithm.gxx,12*sizeof(float));
    memcpy(saved+12,state.algorithm.gxy_re,36*sizeof(float));
    memcpy(saved+48,state.algorithm.gxy_im,36*sizeof(float));
    memcpy(saved+84,state.algorithm.re,24*sizeof(float));
    memcpy(saved+108,state.algorithm.im,24*sizeof(float));
    params[0].value.i32=10;
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG &&
        !memcmp(&snapshot,&state,sizeof(state)) && live==5);
    params[0].value.i32=8;
    inputs[0]=NULL;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);inputs[0]=&in;
    outputs[0]=NULL;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);outputs[0]=&out;
    in.frame_count=7;CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);in.frame_count=8;
    out.frame_count=7;out.frame_capacity=7;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    out.frame_count=8;out.frame_capacity=8;
    void* aliases[]={input+1,&state,state.algorithm.gxx,state.algorithm.gxy_re,
        state.algorithm.gxy_im,state.algorithm.re,state.algorithm.im};
    for (unsigned i=0;i<sizeof(aliases)/sizeof(aliases[0]);++i) {
        out.data=aliases[i];CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    }
    out.data=output;
    for (unsigned i=1;i<sizeof(aliases)/sizeof(aliases[0]);++i) {
        in.data=aliases[i];CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    }
    in.data=input;
    CHECK(result.calls==68 && !memcmp(&snapshot,&state,sizeof(state)) &&
        !memcmp(saved,state.algorithm.gxx,12*sizeof(float)) &&
        !memcmp(saved+12,state.algorithm.gxy_re,36*sizeof(float)) &&
        !memcmp(saved+48,state.algorithm.gxy_im,36*sizeof(float)) &&
        !memcmp(saved+84,state.algorithm.re,24*sizeof(float)) &&
        !memcmp(saved+108,state.algorithm.im,24*sizeof(float)));
    ctx.frame_count=0;inputs[0]=NULL;outputs[0]=NULL;
    CHECK(api->process(handle,&ctx)==0 && !memcmp(&snapshot,&state,sizeof(state)));
    state.algorithm.matrix[0]=NAN;
    CHECK(api->get_parameter(handle,"coherence",&json)==ORPHEUS_ERR_INVALID_ARG);
    state.algorithm.hist[0]=NAN;
    CHECK(api->get_parameter(handle,"history",&hist)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->reset(handle)==0 && state.fft_size==8 && state.smoothing==3 && state.channels==3 &&
        state.algorithm.hist_count==0 && state.algorithm.hist_pos==0);
    CHECK(api->get_parameter(handle,"history",&hist)==0 && !strcmp(hist.value.str,"[]"));
    CHECK(api->get_parameter(handle,"coherence",&json)==0 &&
        !strcmp(json.value.str,"{\"n\":3,\"bins\":4,\"matrix\":[0,0,0,0,0,0,0,0,0]}"));
    return api->destroy(handle);
}

int main(void) {
    CHECK(allocation_failures()==0);
    CHECK(numerical_and_errors()==0 && live==0);
    return 0;
}
