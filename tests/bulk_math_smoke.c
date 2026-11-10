#include "component_state.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); return 1; } } while (0)
static int calls, downstream;
static int emit(void* user, uint32_t port, const OrpheusDelivery* delivery) {
    (void)user;
    if (port!=1 || !delivery || delivery->frame_index!=17) return ORPHEUS_ERR_INVALID_ARG;
    ++calls;
    return downstream;
}
int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    orpheus_builtin_bulk_math_State state;
    const char* keys[]={"channels","elements","operation","a_default","b_default"};
    OrpheusValue params[]={
        {.type=ORPHEUS_VALUE_INT,.value.i32=128}, {.type=ORPHEUS_VALUE_INT,.value.i32=65536},
        {.type=ORPHEUS_VALUE_INT,.value.i32=0},
        {.type=ORPHEUS_VALUE_FLOAT,.value.f32=3}, {.type=ORPHEUS_VALUE_FLOAT,.value.f32=4}
    };
    OrpheusConfig config={.state_block=&state,.param_ids=keys,.param_values=params,.param_count=5};
    void* instance=NULL;
    CHECK(api->create(&instance,&config)==ORPHEUS_OK);
    size_t count=(size_t)128*65536;
    float* a=(float*)malloc(count*sizeof(float));
    float* b=(float*)malloc(count*sizeof(float));
    float* result=(float*)malloc(count*sizeof(float));
    CHECK(a && b && result);
    for(size_t i=0;i<count;i++) {a[i]=3;b[i]=4;}
    OrpheusBuffer ba={.data=a,.channels=128,.frame_count=65536,.frame_capacity=65536,
        .format=ORPHEUS_FORMAT_F32,.interleaved=true};
    OrpheusBuffer bb=ba, out=ba; bb.data=b;out.data=result;out.frame_count=0;
    const OrpheusBuffer* inputs[]={&ba,&bb};
    OrpheusBuffer* outputs[]={&out};
    OrpheusContinuation continuation={.emit=emit};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=2,.output_count=1,
        .frame_count=8,.frame_index=17,.continuation=&continuation};
    const float expected[]={7,-1,12,.75f,5};
    for(int operation=0;operation<5;operation++) {
        params[2].value.i32=operation;
        CHECK(api->prepare(instance,&config)==ORPHEUS_OK);
        calls=0;
        CHECK(api->process(instance,&ctx)==ORPHEUS_OK && calls==1 && out.frame_count==65536);
        for(size_t i=0;i<count;i++) CHECK(result[i]==expected[operation]);
    }
    params[1].value.i32=65537;
    CHECK(api->prepare(instance,&config)==ORPHEUS_ERR_INVALID_ARG && state.elements==65536);
    params[1].value.i32=65536;
    out.data=a+1;calls=0;
    CHECK(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG && calls==0 && a[1]==3);
    out.data=result;
    bb.frame_count=65535; CHECK(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG);bb.frame_count=65536;
    ba.format=ORPHEUS_FORMAT_I32; CHECK(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG);ba.format=ORPHEUS_FORMAT_F32;
    out.frame_capacity=65535; CHECK(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG);out.frame_capacity=65536;
    ctx.frame_count=0;calls=0;
    CHECK(api->process(instance,&ctx)==ORPHEUS_OK && calls==0);ctx.frame_count=8;
    outputs[0]=NULL;downstream=-77;
    CHECK(api->process(instance,&ctx)==-77 && calls==1);downstream=0;outputs[0]=&out;
    inputs[0]=inputs[1]=NULL;
    CHECK(api->process(instance,&ctx)==ORPHEUS_OK);
    for(size_t i=0;i<count;i++) CHECK(result[i]==5);
    params[0].value.i32=1;params[1].value.i32=1;
    ba.channels=bb.channels=out.channels=1;
    ba.frame_count=bb.frame_count=1;
    inputs[0]=&ba;inputs[1]=&bb;
    const float special_a[]={-0.0f,-0.0f,1.0f,0.0f,INFINITY,3.0f,NAN};
    const float special_b[]={-0.0f,0.0f,0.0f,0.0f,INFINITY,NAN,4.0f};
    const int operations[]={0,1,3,3,4,4,4};
    const float special_expected[]={-0.0f,-0.0f,INFINITY,NAN,INFINITY,3.0f*1.41421354f,NAN};
    for(unsigned i=0;i<sizeof(operations)/sizeof(operations[0]);i++) {
        params[2].value.i32=operations[i];
        CHECK(api->prepare(instance,&config)==ORPHEUS_OK);
        a[0]=special_a[i];b[0]=special_b[i];
        CHECK(api->process(instance,&ctx)==ORPHEUS_OK);
        uint32_t actual, reference;
        memcpy(&actual,result,sizeof(actual));
        memcpy(&reference,special_expected+i,sizeof(reference));
        CHECK(isnan(special_expected[i]) ? isnan(result[0]) : actual==reference);
    }
    CHECK(api->reset(instance)==ORPHEUS_OK && api->destroy(instance)==ORPHEUS_OK);
    free(a);free(b);free(result);
    return 0;
}
