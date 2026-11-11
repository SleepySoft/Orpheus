#include "component_state.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "检查失败，行%d: %s\n", __LINE__, #expr); return 1; \
} } while (0)

typedef orpheus_builtin_n_way_mux_State NWayMuxState;

static int immediate_selection(unsigned channels) {
    const OrpheusComponentInterface* iface = orpheus_get_interface();
    NWayMuxState state = {0};
    const char* ids[] = {"inputs", "select", "ramp_ms"};
    OrpheusValue values[] = {
        {.type=ORPHEUS_VALUE_INT, .value.i32=2},
        {.type=ORPHEUS_VALUE_INT, .value.i32=2},
        {.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.0f}
    };
    OrpheusConfig config = {.channels=channels, .sample_rate=48000, .block_size=32,
        .param_count=3, .param_ids=ids, .param_values=values,.state_block=&state};
    void* instance=NULL;
    CHECK(iface->create(&instance,&config)==ORPHEUS_OK && instance==&state);
    CHECK(iface->prepare(&state, &config) == ORPHEUS_OK);
    float a[32*64], b[32*64], output[32*64+1];
    unsigned count = 32*channels;
    size_t bytes = count*sizeof(float);
    output[count] = 123.0f;
    for (unsigned i = 0; i < count; ++i) {
        a[i] = i % 3 == 0 ? -0.0f : (float)((int)i - 160) / 512.0f;
        if (i % 7 == 0) a[i] = INFINITY;
        if (i % 11 == 0) a[i] = -INFINITY;
        b[i] = NAN;
    }
    OrpheusBuffer ba = {.data=a, .channels=channels, .format=ORPHEUS_FORMAT_F32,
        .frame_count=32, .frame_capacity=32, .interleaved=true};
    OrpheusBuffer bb = ba, out = ba;
    bb.data = b;
    out.data = output;
    const OrpheusBuffer* inputs[] = {&ba, &bb};
    OrpheusBuffer* outputs[] = {&out};
    OrpheusProcessContext ctx = {.inputs=inputs, .input_count=2,
        .outputs=outputs, .output_count=1, .frame_count=32};
    OrpheusValue select = {.type=ORPHEUS_VALUE_INT, .value.i32=1};
    CHECK(iface->set_parameter(&state, "select", &select) == ORPHEUS_OK);
    CHECK(iface->process(&state, &ctx) == ORPHEUS_OK);
    CHECK(memcmp(a, output, bytes) == 0);
    select.value.i32 = 2;
    CHECK(iface->set_parameter(&state, "select", &select) == ORPHEUS_OK);
    CHECK(iface->reset(&state) == ORPHEUS_OK);
    CHECK(iface->process(&state, &ctx) == ORPHEUS_OK);
    CHECK(memcmp(b, output, bytes) == 0);
    select.value.i32 = 1;
    CHECK(iface->set_parameter(&state, "select", &select) == ORPHEUS_OK);
    CHECK(iface->reset(&state) == ORPHEUS_OK);
    CHECK(iface->process(&state, &ctx) == ORPHEUS_OK);
    CHECK(memcmp(a, output, bytes) == 0);
    select.value.i32 = 2;
    CHECK(iface->set_parameter(&state, "select", &select) == ORPHEUS_OK);
    inputs[1] = NULL;
    CHECK(iface->process(&state, &ctx) == ORPHEUS_OK);
    for (unsigned i = 0; i < count; ++i) CHECK(output[i] == 0.0f && !signbit(output[i]));
    CHECK(output[count] == 123.0f);
    config.channels = 65;
    CHECK(iface->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    config.channels = 0;
    CHECK(iface->prepare(&state, &config) == ORPHEUS_OK);
    CHECK(state.channels == 2);
    CHECK(iface->destroy(&state)==ORPHEUS_OK);
    return 0;
}

static int fading_selection(void) {
    const OrpheusComponentInterface* iface = orpheus_get_interface();
    NWayMuxState state = {0};
    const char* ids[] = {"inputs", "select", "ramp_ms"};
    OrpheusValue values[] = {
        {.type=ORPHEUS_VALUE_INT, .value.i32=2},
        {.type=ORPHEUS_VALUE_INT, .value.i32=1},
        {.type=ORPHEUS_VALUE_FLOAT, .value.f32=4.0f}
    };
    OrpheusConfig config = {.channels=1, .sample_rate=1000, .block_size=8,
        .param_count=3, .param_ids=ids, .param_values=values,.state_block=&state};
    void* instance=NULL;
    CHECK(iface->create(&instance,&config)==ORPHEUS_OK && instance==&state);
    CHECK(iface->prepare(&state, &config) == ORPHEUS_OK);
    float a[8] = {0}, b[8], output[8];
    for (unsigned i = 0; i < 8; ++i) b[i] = 1.0f;
    OrpheusBuffer ba = {.data=a, .channels=1, .format=ORPHEUS_FORMAT_F32,
        .frame_count=8, .frame_capacity=8, .interleaved=true};
    OrpheusBuffer bb = ba, out = ba;
    bb.data = b;
    out.data = output;
    const OrpheusBuffer* inputs[] = {&ba, &bb};
    OrpheusBuffer* outputs[] = {&out};
    OrpheusProcessContext ctx = {.inputs=inputs, .input_count=2,
        .outputs=outputs, .output_count=1, .frame_count=8};
    OrpheusValue select = {.type=ORPHEUS_VALUE_INT, .value.i32=2};
    CHECK(iface->set_parameter(&state, "select", &select) == ORPHEUS_OK);
    CHECK(iface->process(&state, &ctx) == ORPHEUS_OK);
    for (unsigned i = 0; i < 8; ++i) {
        float wanted = i < 4 ? (float)(i + 1) / 4.0f : 1.0f;
        CHECK(output[i] == wanted);
    }
    CHECK(iface->destroy(&state)==ORPHEUS_OK);
    return 0;
}

typedef struct { unsigned calls,frames; int result; } Observer;
static int observe(void* user,uint32_t port,const OrpheusDelivery* delivery) {
    Observer* observer=(Observer*)user;
    observer->calls++;
    if (port!=1 || !delivery || !delivery->buffer ||
        delivery->buffer->frame_count!=observer->frames || delivery->frame_index!=17 ||
        delivery->epoch!=7) return ORPHEUS_ERR_INVALID_ARG;
    return observer->result;
}

static int delivery_and_parameters(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    NWayMuxState state={0};
    const char* keys[]={"inputs","ramp_ms"};
    OrpheusValue params[]={
        {.type=ORPHEUS_VALUE_INT,.value.i32=3},
        {.type=ORPHEUS_VALUE_FLOAT,.value.f32=16}
    };
    OrpheusConfig config={.state_block=&state,.channels=1,.block_size=8,.sample_rate=1000,
        .param_count=2,.param_ids=keys,.param_values=params};
    void* instance=NULL;
    CHECK(api->create(&instance,&config)==ORPHEUS_OK && instance==&state);
    CHECK(api->prepare(instance,&config)==ORPHEUS_OK);
    OrpheusValue request={.type=ORPHEUS_VALUE_INT,.value.i32=INT32_MAX},actual;
    CHECK(api->set_parameter(instance,"select",&request)==ORPHEUS_OK);
    CHECK(api->get_parameter(instance,"select",&actual)==ORPHEUS_OK && actual.value.i32==3);
    CHECK(api->get_parameter(instance,"select_index",&actual)==ORPHEUS_OK && actual.value.i32==2);
    float a[8]={0},b[8],output[9];
    for(unsigned i=0;i<8;i++) b[i]=1;
    output[8]=123;
    OrpheusBuffer ba={.data=a,.channels=1,.frame_count=8,.frame_capacity=8,
        .format=ORPHEUS_FORMAT_F32,.interleaved=true},bb=ba,out=ba;
    bb.data=b;out.data=output;
    const OrpheusBuffer* inputs[]={&ba,&bb,NULL};
    OrpheusBuffer* outputs[]={&out};
    Observer observer={.frames=0,.result=ORPHEUS_OK};
    OrpheusContinuation continuation={.user=&observer,.emit=observe};
    OrpheusProcessContext ctx={.inputs=inputs,.input_count=3,.outputs=outputs,.output_count=1,
        .frame_count=0,.frame_index=17,.epoch=7,.continuation=&continuation};
    NWayMuxState before=state;
    CHECK(api->process(instance,&ctx)==ORPHEUS_OK && observer.calls==1 &&
        !memcmp(&before,&state,sizeof(state)));
    ctx.frame_count=8;
    ba.frame_count=7;
    CHECK(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG && observer.calls==1 &&
        !memcmp(&before,&state,sizeof(state)));
    ba.frame_count=8;out.data=a;
    CHECK(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG &&
        !memcmp(&before,&state,sizeof(state)));
    out.data=a+1;out.frame_capacity=7;
    CHECK(api->process(instance,&ctx)==ORPHEUS_ERR_INVALID_ARG &&
        !memcmp(&before,&state,sizeof(state)));
    out.data=output;out.frame_capacity=8;
    observer.frames=8;observer.result=ORPHEUS_ERR_PROCESSING;
    CHECK(api->process(instance,&ctx)==ORPHEUS_ERR_PROCESSING && observer.calls==2);
    CHECK(state.select_pos==.5f && output[8]==123);
    for(unsigned n=0;n<8;n++) CHECK(output[n]==(float)(n+1)/16);
    request.value.i32=INT32_MIN;
    CHECK(api->set_parameter(instance,"select_index",&request)==ORPHEUS_OK);
    CHECK(state.select==1 && state.select_index==0 && state.select_pos==.5f);
    CHECK(api->reset(instance)==ORPHEUS_OK && state.select_pos==0);
    before=state;
    request.type=ORPHEUS_VALUE_FLOAT;request.value.f32=NAN;
    CHECK(api->set_parameter(instance,"select",&request)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->set_parameter(instance,"select_pos",&request)==ORPHEUS_ERR_UNSUPPORTED);
    CHECK(!memcmp(&before,&state,sizeof(state)));
    params[1].value.f32=NAN;
    CHECK(api->prepare(instance,&config)==ORPHEUS_ERR_INVALID_ARG &&
        !memcmp(&before,&state,sizeof(state)));
    CHECK(api->destroy(instance)==ORPHEUS_OK);
    return 0;
}

int main(void) {
    CHECK(immediate_selection(10) == 0);
    CHECK(immediate_selection(40) == 0);
    CHECK(immediate_selection(52) == 0);
    CHECK(immediate_selection(64) == 0);
    CHECK(fading_selection() == 0);
    CHECK(delivery_and_parameters() == 0);
    return 0;
}
