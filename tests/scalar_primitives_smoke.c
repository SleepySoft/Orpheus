#include "../components/orpheus/builtin/scalar_math/generated/component_state.h"
#include "../components/orpheus/builtin/bulk_constant/generated/component_state.h"
#include "../components/orpheus/builtin/bulk_lookup/generated/component_state.h"
#include "../components/orpheus/builtin/scalar_memory/generated/component_state.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const OrpheusComponentInterface* scalar_api(void);
const OrpheusComponentInterface* constant_api(void);
const OrpheusComponentInterface* lookup_api(void);
const OrpheusComponentInterface* memory_api(void);
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); return 1; } } while (0)
static unsigned calls;
static unsigned ports, fail_port;
static unsigned feedback_fault;
static float redirected=42;
static int downstream;
static int emit(void* user, uint32_t port, const OrpheusDelivery* delivery) {
    (void)user;
    ++calls;
    if (!port || port > 3 || !delivery) return 99;
    ports = ports*10+port;
    return fail_port == port ? -77 : downstream;
}
static OrpheusBuffer buffer(void* data, unsigned count) {
    OrpheusBuffer b={.data=data, .channels=1, .frame_count=count, .frame_capacity=count,
        .format=ORPHEUS_FORMAT_F32, .interleaved=true};
    return b;
}
static bool same(float a, float b) {
    return (isnan(a) && isnan(b)) || !memcmp(&a, &b, sizeof(a));
}
typedef struct { OrpheusBuffer* input; float* old; float* candidate; } Feedback;
static int feedback_emit(void* user, uint32_t port, const OrpheusDelivery* delivery) {
    Feedback* f=(Feedback*)user; ++calls;
    if (port!=1 || !delivery) return 99;
    if (downstream) return downstream;
    *f->candidate=*f->old+2.0f; f->input->frame_count=1;
    if (feedback_fault==1) f->input->data=NULL;
    if (feedback_fault==2) f->input->format=ORPHEUS_FORMAT_I32;
    if (feedback_fault==3) f->input->frame_count=0;
    if (feedback_fault==4) f->input->data=&redirected;
    return 0;
}
static int memory_contract(void) {
    const OrpheusComponentInterface* api=memory_api();
    orpheus_builtin_scalar_memory_State state;
    const char* keys[]={"initial_value", "write_after_call"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_FLOAT,.value.f32=3},
                          {.type=ORPHEUS_VALUE_BOOL,.value.b=true}};
    OrpheusConfig config={.state_block=&state,.param_count=2,.param_ids=keys,.param_values=params};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && handle==&state && api->prepare(handle,&config)==0);
    float candidate=99,old=99,initial=10; int32_t update=1;
    OrpheusBuffer in=buffer(&candidate,1),gate=buffer(&update,1),seed=buffer(&initial,1),out=buffer(&old,1);
    gate.format=ORPHEUS_FORMAT_I32;
    int32_t gate_value=-99; OrpheusBuffer forwarded=buffer(&gate_value,1); forwarded.format=ORPHEUS_FORMAT_I32;
    const OrpheusBuffer* inputs[]={&in,&gate,NULL}; OrpheusBuffer* outputs[]={&out,&forwarded};
    Feedback f={&in,&old,&candidate};
    OrpheusContinuation continuation={.emit=feedback_emit,.user=&f};
    OrpheusProcessContext context={.inputs=inputs,.outputs=outputs,.input_count=3,.output_count=2,
        .frame_count=64,.continuation=&continuation};
    in.frame_count=0; calls=0;
    CHECK(api->process(handle,&context)==0 && calls==1 && old==3 && state.algorithm.value==5 && state.algorithm.initialized && gate_value==1);
    CHECK(api->process(handle,&context)==0 && old==5 && state.algorithm.value==7);
    update=0; CHECK(api->process(handle,&context)==0 && old==7 && state.algorithm.value==7 && gate_value==0);
    update=1; downstream=-77;
    CHECK(api->process(handle,&context)==-77 && state.algorithm.value==7); downstream=0;
    CHECK(api->reset(handle)==0 && !state.algorithm.initialized && state.algorithm.value==0);
    inputs[2]=&seed; CHECK(api->process(handle,&context)==0 && old==10 && state.algorithm.value==12);
    initial=-9; CHECK(api->process(handle,&context)==0 && old==12 && state.algorithm.value==14);
    CHECK(api->reset(handle)==0);
    gate.format=ORPHEUS_FORMAT_F32; calls=0;
    CHECK(api->process(handle,&context)!=0 && calls==0 && !state.algorithm.initialized);
    gate.format=ORPHEUS_FORMAT_I32; out.data=&candidate;
    CHECK(api->process(handle,&context)!=0); out.data=&old;
    forwarded.data=&update; CHECK(api->process(handle,&context)!=0); forwarded.data=&gate_value;
    forwarded.data=&old; CHECK(api->process(handle,&context)!=0); forwarded.data=&gate_value;
    params[0].value.f32=NAN;
    CHECK(api->prepare(handle,&config)!=0 && state.write_after_call && !state.algorithm.initialized);
    params[0].value.f32=3; params[1].value.b=false;
    CHECK(api->prepare(handle,&config)==0); continuation.emit=emit;
    update=0; in.frame_count=0;
    CHECK(api->process(handle,&context)==0 && old==0 && !state.algorithm.initialized);
    update=1; in.frame_count=1;
    candidate=NAN; CHECK(api->process(handle,&context)==0 && isnan(old) && isnan(state.algorithm.value));
    update=0; candidate=5;
    CHECK(api->process(handle,&context)==0 && isnan(old) && isnan(state.algorithm.value));
    update=1; candidate=-0.0f; outputs[0]=NULL; calls=0;
    CHECK(api->process(handle,&context)==0 && calls==1 && signbit(state.algorithm.value));
    in.frame_count=0; CHECK(api->process(handle,&context)!=0 && signbit(state.algorithm.value));
    in.frame_count=1; context.frame_count=0; calls=0;
    CHECK(api->process(handle,&context)==0 && calls==0);
    context.frame_count=64; candidate=17; downstream=-77;
    CHECK(api->process(handle,&context)==-77 && state.algorithm.value==17); downstream=0;
    params[1].value.b=true; CHECK(api->prepare(handle,&config)==0);
    inputs[2]=NULL; params[0].value.f32=-4;
    CHECK(api->set_parameter(handle,"initial_value",params)==0);
    OrpheusValue readback={0};
    CHECK(api->get_parameter(handle,"initial_value",&readback)==0 && readback.value.f32==-4);
    CHECK(api->set_parameter(handle,"write_after_call",params+1)!=0);
    update=-2; candidate=17; in.frame_count=1;
    CHECK(api->process(handle,&context)==0 && state.algorithm.value==17 && gate_value==-2);
    CHECK(api->reset(handle)==0);
    continuation.emit=feedback_emit; outputs[0]=&out; in.frame_count=0;
    CHECK(api->process(handle,&context)==0 && old==-4 && state.algorithm.value==-2);
    for (feedback_fault=1; feedback_fault<=3; ++feedback_fault) {
        CHECK(api->reset(handle)==0);
        in.data=&candidate; in.format=ORPHEUS_FORMAT_F32; in.frame_count=0;
        CHECK(api->process(handle,&context)==ORPHEUS_ERR_INVALID_ARG
            && !state.algorithm.initialized && state.algorithm.value==0);
    }
    in.data=&candidate; in.format=ORPHEUS_FORMAT_F32; in.frame_count=0;
    feedback_fault=4;
    CHECK(api->process(handle,&context)==0 && state.algorithm.value==42);
    feedback_fault=0;
    CHECK(api->destroy(handle)==0);
    return 0;
}
int main(void) {
    CHECK(memory_contract()==0);
    orpheus_builtin_scalar_math_State scalar_state;
    OrpheusConfig config={.state_block=&scalar_state};
    OrpheusContinuation continuation={.emit=emit};
    float data[]={-0.0f, NAN, 13.0f}, result=99;
    OrpheusBuffer a=buffer(data, 1), b=buffer(data+1, 1), c=buffer(data+2, 1), out=buffer(&result, 1);
    const OrpheusBuffer* inputs[]={&a, &b, &c};
    OrpheusBuffer* outputs[]={&out};
    OrpheusProcessContext context={.inputs=inputs, .outputs=outputs, .input_count=3,
        .output_count=1, .frame_count=64, .continuation=&continuation};
    const OrpheusComponentInterface* api=scalar_api();
    void* state=NULL;
    CHECK(api->create(&state, &config)==0 && api->prepare(state, &config)==0);
    OrpheusValue operation={.type=ORPHEUS_VALUE_INT};
    for (int op=0; op<6; ++op) {
        operation.value.i32=op; CHECK(api->set_parameter(state, "operation", &operation)==0);
        float expected[]={fminf(data[0], data[1]), fmaxf(data[0], data[1]), 0, 1, data[2], data[0]};
        calls=0;
        CHECK(api->process(state, &context)==0 && calls==1 && same(result, expected[op]));
    }
    data[0]=6; data[1]=2;
    for (int op=6; op<12; ++op) {
        const float expected[]={8,4,12,3,6,log10f(6)};
        operation.value.i32=op; CHECK(api->set_parameter(state,"operation",&operation)==0);
        CHECK(api->process(state,&context)==0 && same(result,expected[op-6]));
    }
    operation.value.i32=9; CHECK(api->set_parameter(state,"operation",&operation)==0);
    data[1]=0; CHECK(api->process(state,&context)==0 && isinf(result));
    operation.value.i32=11; CHECK(api->set_parameter(state,"operation",&operation)==0);
    data[0]=-1; CHECK(api->process(state,&context)==0 && isnan(result));
    operation.value.i32=4; CHECK(api->set_parameter(state, "operation", &operation)==0);
    data[0]=NAN; data[1]=-7;
    CHECK(api->process(state, &context)==0 && result==-7);
    inputs[0]=NULL; inputs[1]=NULL; inputs[2]=NULL;
    OrpheusValue fallback={.type=ORPHEUS_VALUE_FLOAT, .value.f32=8};
    CHECK(api->set_parameter(state, "c_default", &fallback)==0);
    CHECK(api->process(state, &context)==0 && result==8);
    CHECK(api->reset(state)==0 && api->process(state, &context)==0 && result==8);
    fallback.value.f32=NAN; CHECK(api->set_parameter(state, "c_default", &fallback)!=0);
    const char* bad_key="unknown"; config.param_count=1; config.param_ids=&bad_key; config.param_values=&fallback;
    CHECK(api->prepare(state, &config)!=0 && api->process(state, &context)==0 && result==8);
    config.param_count=0; inputs[0]=&a; inputs[1]=&b; inputs[2]=&c;
    calls=0; a.frame_count=0;
    CHECK(api->process(state, &context)!=0 && calls==0); a.frame_count=1;
    out.data=data; CHECK(api->process(state, &context)!=0); out.data=&result;
    outputs[0]=NULL; calls=0; downstream=-77;
    CHECK(api->process(state, &context)==-77 && calls==1); downstream=0;
    outputs[0]=&out; context.frame_count=0; calls=0;
    CHECK(api->process(state, &context)==0 && calls==0); context.frame_count=64;
    CHECK(api->destroy(state)==0);

    api=constant_api();
    orpheus_builtin_bulk_constant_State external;
    config.state_block=&external;
    CHECK(api->create(&state, &config)==0 && state==&external && api->prepare(state, &config)==0);
    float table[4]={0}, first=-1, last=-1;
    OrpheusBuffer full=buffer(table, 4), head=buffer(&first, 1), tail=buffer(&last, 1);
    OrpheusBuffer* table_outputs[]={&full, &head, &tail};
    context.outputs=table_outputs; context.output_count=3;
    CHECK(api->process(state, &context)!=0);
    context.inputs=NULL; context.input_count=0;
    ports=0; calls=0;
    CHECK(api->process(state, &context)==0 && full.frame_count==2 && table[0]==0 && table[1]==1 && first==0 && last==1);
    CHECK(ports==123 && calls==3);
    const char* keys[]={"elements", "values"};
    OrpheusValue values[]={{.type=ORPHEUS_VALUE_INT, .value.i32=4},
        {.type=ORPHEUS_VALUE_STRING, .value.str="-2, -0, 3.5, 9"}};
    config.param_count=2; config.param_ids=keys; config.param_values=values;
    CHECK(api->prepare(state, &config)==0 && api->process(state, &context)==0 && first==-2 && last==9);
    const char* invalid[]={"1,2", "1,2,3,4,5", "1,2,,4", "1,2,3,4,", "1,2,nan,4", "1,2,inf,4", "1,2,bad,4", "1,2,3,1e99", "1,2,3,1e-9999"};
    for (unsigned i=0; i<sizeof(invalid)/sizeof(*invalid); ++i) {
        values[1].value.str=invalid[i];
        CHECK(api->prepare(state, &config)!=0);
        CHECK(api->process(state, &context)==0 && full.frame_count==4 && last==9 && signbit(table[1]));
    }
    values[1].value.str="-2, -0, 3.5, 9";
    full.frame_capacity=3; calls=0;
    CHECK(api->process(state, &context)!=0 && calls==0); full.frame_capacity=4;
    tail.data=table+1; CHECK(api->process(state, &context)!=0); tail.data=&last;
    full.data=external.algorithm.values; CHECK(api->process(state, &context)!=0); full.data=table;
    fail_port=2; calls=0; ports=0;
    CHECK(api->process(state, &context)==-77 && calls==2 && ports==12); fail_port=0;
    table_outputs[0]=NULL; table_outputs[1]=NULL; table_outputs[2]=NULL;
    downstream=-77; calls=0;
    CHECK(api->process(state, &context)==-77 && calls==1); downstream=0;
    CHECK(api->reset(state)==0 && api->destroy(state)==0);

    api=lookup_api();
    orpheus_builtin_bulk_lookup_State lookup_state;
    config=(OrpheusConfig){.state_block=&lookup_state};
    CHECK(api->create(&state, &config)==0 && api->prepare(state, &config)==0);
    float x=0, axis[]={0, 2, 5, 7}, ys[]={-1, 3, 3, 19};
    a=buffer(&x, 1); b=buffer(axis, 2); c=buffer(ys, 2);
    inputs[0]=&a; inputs[1]=&b; inputs[2]=&c;
    context.inputs=inputs; context.input_count=3;
    context.outputs=outputs; context.output_count=1;
    const float queries[]={-INFINITY, -1, 0, .5f, 1, 2, INFINITY, NAN};
    const float expected[]={0, 0, -1, 0, 1, 3, 0, 0};
    for (unsigned i=0; i<8; ++i) {
        x=queries[i]; CHECK(api->process(state, &context)==0 && same(result, expected[i]));
    }
    const char* points="points";
    OrpheusValue count={.type=ORPHEUS_VALUE_INT, .value.i32=4};
    config.param_count=1; config.param_ids=&points; config.param_values=&count;
    CHECK(api->prepare(state, &config)==0);
    b=buffer(axis, 4); c=buffer(ys, 4);
    x=4; CHECK(api->process(state, &context)==0 && result==3);
    for (unsigned i=0; i<2; ++i) {
        float t=axis[i]; axis[i]=axis[3-i]; axis[3-i]=t;
        t=ys[i]; ys[i]=ys[3-i]; ys[3-i]=t;
    }
    CHECK(api->process(state, &context)==0 && result==3);
    x=7; CHECK(api->process(state, &context)==0 && result==19);
    count.value.i32=1; CHECK(api->prepare(state, &config)!=0);
    CHECK(api->process(state, &context)==0 && result==19);
    axis[1]=axis[0]; calls=0; result=71;
    CHECK(api->process(state, &context)!=0 && calls==0 && result==71); axis[1]=5;
    axis[1]=NAN; CHECK(api->process(state, &context)!=0); axis[1]=5;
    c.frame_count=3; CHECK(api->process(state, &context)!=0); c.frame_count=4;
    out.data=ys; CHECK(api->process(state, &context)!=0); out.data=&result;
    outputs[0]=NULL; downstream=-77; calls=0;
    CHECK(api->process(state, &context)==-77 && calls==1); downstream=0;
    context.frame_count=0; calls=0;
    CHECK(api->process(state, &context)==0 && calls==0); context.frame_count=64;
    CHECK(api->reset(state)==0 && api->destroy(state)==0);
    return 0;
}
