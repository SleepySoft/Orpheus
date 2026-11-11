#include "component_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%d: %s\n", __LINE__, #expr); return 1; } } while (0)

static int slots_seen;
static OrpheusSlotId check_slot(void* ctx, const OrpheusSlotInfo* slot) {
    (void)ctx;
    if (strcmp(slot->key, "delay_samples") == 0 &&
        slot->type == ORPHEUS_VALUE_INT && slot->count == 1 &&
        !(slot->flags & ORPHEUS_SLOT_DIRECT_WRITE)) ++slots_seen;
    return 0;
}

static int test_integer_vector(const OrpheusComponentInterface* iface) {
    const char* ids[] = {"channels", "max_delay_samples", "delay_vector_mode", "initial_delays_i32"};
    OrpheusValue values[] = {
        {.type=ORPHEUS_VALUE_INT, .value.i32=3},
        {.type=ORPHEUS_VALUE_INT, .value.i32=16},
        {.type=ORPHEUS_VALUE_INT, .value.i32=1},
        {.type=ORPHEUS_VALUE_STRING, .value.str="0,3,16"}
    };
    OrpheusConfig config = {.sample_rate=48000, .block_size=7, .param_count=4,
                           .param_ids=ids, .param_values=values};
    orpheus_builtin_delay_line_State state = {0};
    config.state_block=&state;
    void* instance=NULL;
    CHECK(iface->create(&instance,&config)==ORPHEUS_OK);
    CHECK(iface->prepare(&state, &config) == ORPHEUS_OK);
    float input[21], output[21], history[512][3] = {{0}};
    OrpheusBuffer in = {.data=input, .channels=3, .format=ORPHEUS_FORMAT_F32, .frame_capacity=7, .interleaved=true};
    OrpheusBuffer out = {.data=output, .channels=3, .format=ORPHEUS_FORMAT_F32, .frame_capacity=7, .interleaved=true};
    const OrpheusBuffer* inputs[] = {&in};
    OrpheusBuffer* outputs[] = {&out};
    OrpheusProcessContext context = {.inputs=inputs, .outputs=outputs, .input_count=1, .output_count=1};
    for (unsigned pass = 0; pass < 2; ++pass) {
        unsigned cursor = 0;
        if (pass) {
            CHECK(iface->reset(&state) == ORPHEUS_OK);
            CHECK(state.delay_vector_mode == 1 && state.delays_i32[0] == 0 &&
                  state.delays_i32[1] == 3 && state.delays_i32[2] == 16);
            memset(history, 0, sizeof(history));
        }
        for (unsigned block = 0; block < 64; ++block) {
            unsigned frames = block % 2 ? 3 : 7;
            for (unsigned n = 0; n < frames; ++n)
                for (unsigned c = 0; c < 3; ++c)
                    history[cursor+n][c] = input[n*3+c] = (float)(cursor+n+1) * (float)(c+1);
            context.frame_count = in.frame_count = frames;
            CHECK(iface->process(&state, &context) == ORPHEUS_OK);
            for (unsigned n = 0; n < frames; ++n)
                for (unsigned c = 0; c < 3; ++c) {
                    unsigned d = (unsigned)state.delays_i32[c];
                    CHECK(output[n*3+c] == (cursor+n >= d ? history[cursor+n-d][c] : 0.0f));
                }
            cursor += frames;
        }
    }
    CHECK(iface->destroy(&state) == ORPHEUS_OK);
    const char* invalid[] = {"1,2", "1,2,3,4", "1.0,2,3", "1,2,17", "1,-1,3",
                            "1,2,3x", "1,2,", "1,,3", "2147483648,2,3"};
    for (unsigned i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
        orpheus_builtin_delay_line_State rejected = {0};
        values[3].value.str = invalid[i];
        config.state_block=&rejected;
        CHECK(iface->create(&instance, &config) == ORPHEUS_ERR_INVALID_ARG);
        CHECK(rejected.algorithm.buffer == NULL);
    }
    values[3].value.str = "";
    memset(&state, 0, sizeof(state));
    config.state_block=&state;
    CHECK(iface->create(&instance,&config)==ORPHEUS_OK);
    CHECK(iface->prepare(&state, &config) == ORPHEUS_OK);
    CHECK(state.delays_i32[0] == 0 && state.delays_i32[1] == 0 && state.delays_i32[2] == 0);
    CHECK(iface->destroy(&state) == ORPHEUS_OK);
    return 0;
}

static float sample(uint32_t frame, uint32_t channel) {
    const uint32_t patterns[]={0,0x80000000u,0x7f800000u,0xff800000u,0x7fc01234u,1};
    float result=(float)((int)((frame*17+channel*31)%103)-51)*.03125f;
    if(frame%17<6) memcpy(&result,patterns+frame%17,sizeof(result));
    return result;
}

static int downstream(void* user, uint32_t port, const OrpheusDelivery* delivery) {
    unsigned* calls=user;
    if(port!=1 || !delivery || !delivery->buffer || delivery->buffer->frame_count!=65536)
        return ORPHEUS_ERR_INVALID_ARG;
    (*calls)++;
    return *calls==3 ? ORPHEUS_ERR_PROCESSING : ORPHEUS_OK;
}

static int test_large_history(const OrpheusComponentInterface* api) {
    const unsigned channels=64,frames=65536;
    const size_t count=(size_t)channels*frames,bytes=count*sizeof(float);
    float* input=malloc(bytes),*output=malloc(bytes);
    CHECK(input && output);
    const char* ids[]={"channels","max_delay_samples","delay_vector_mode","delays_samples"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=64},
        {.type=ORPHEUS_VALUE_INT,.value.i32=192000},{.type=ORPHEUS_VALUE_INT,.value.i32=1},
        {.type=ORPHEUS_VALUE_STRING,.value.str=""}};
    orpheus_builtin_delay_line_State state;
    OrpheusConfig config={.sample_rate=48000,.block_size=frames,.state_block=&state,
        .param_count=4,.param_ids=ids,.param_values=params};
    void* handle=NULL;
    CHECK(api->create(&handle,&config)==0 && api->prepare(handle,&config)==0);
    for(unsigned c=0;c<channels;c++) state.delays_i32[c]=c%3==0 ? 0 : c%3==1 ? 1 : 192000;
    OrpheusBuffer in={.data=input,.channels=channels,.format=ORPHEUS_FORMAT_F32,.interleaved=true,
        .frame_count=frames,.frame_capacity=frames};
    OrpheusBuffer out=in;out.data=output;
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&out};
    unsigned calls=0;
    OrpheusContinuation continuation={.emit=downstream,.user=&calls};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,
        .frame_count=frames,.continuation=&continuation};
    for(unsigned pass=0;pass<4;pass++) {
        for(unsigned n=0;n<frames;n++)
            for(unsigned c=0;c<channels;c++) input[(size_t)n*channels+c]=sample(pass*frames+n,c);
        CHECK(api->process(handle,&ctx)==(pass==2 ? ORPHEUS_ERR_PROCESSING : ORPHEUS_OK));
        CHECK(calls==pass+1);
        for(unsigned n=0;n<frames;n++) for(unsigned c=0;c<channels;c++) {
            unsigned frame=pass*frames+n,delay=(unsigned)state.delays_i32[c];
            float expected=frame>=delay ? sample(frame-delay,c) : 0.0f;
            CHECK(!memcmp(output+(size_t)n*channels+c,&expected,sizeof(float)));
        }
    }
    uint32_t position=state.algorithm.write_pos;
    float* buffer=state.algorithm.buffer;
    params[3].value.str="0,nan";
    CHECK(api->prepare(handle,&config)==ORPHEUS_ERR_INVALID_ARG);
    CHECK(state.algorithm.buffer==buffer && state.algorithm.write_pos==position && !state.delays_samples[0]);
    params[3].value.str="";
    in.frame_count=frames-1;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && state.algorithm.write_pos==position);
    in.frame_count=frames;out.data=input;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && state.algorithm.write_pos==position);
    out.data=output;state.delays_i32[0]=-1;
    CHECK(api->process(handle,&ctx)==ORPHEUS_ERR_INVALID_ARG && state.algorithm.write_pos==position);
    state.delays_i32[0]=0;
    ctx.frame_count=0;inputs[0]=NULL;outputs[0]=NULL;
    CHECK(api->process(handle,&ctx)==0 && state.algorithm.write_pos==position && calls==4);
    CHECK(api->destroy(handle)==0);
    free(input);free(output);
    return 0;
}

int main(void) {
    const OrpheusComponentInterface* iface = orpheus_get_interface();
    const char* ids[] = {"channels", "max_delay_samples", "delays_samples", "delay_samples"};
    OrpheusValue values[] = {
        {.type=ORPHEUS_VALUE_INT, .value.i32=3},
        {.type=ORPHEUS_VALUE_INT, .value.i32=16},
        {.type=ORPHEUS_VALUE_STRING, .value.str="1,3,7"},
        {.type=ORPHEUS_VALUE_INT, .value.i32=5}
    };
    orpheus_builtin_delay_line_State state = {0};
    OrpheusConfig config = {
        .sample_rate=48000, .block_size=7, .channels=3,
        .param_count=4, .param_ids=ids, .param_values=values, .state_block=&state
    };
    void* instance = NULL;
    float input[21], output[21], history[512][3] = {{0}};
    const unsigned legacy[] = {1, 3, 7};
    OrpheusBuffer in = {.data=input, .channels=3, .format=ORPHEUS_FORMAT_F32, .frame_capacity=7, .interleaved=true};
    OrpheusBuffer out = {.data=output, .channels=3, .format=ORPHEUS_FORMAT_F32, .frame_capacity=7, .interleaved=true};
    const OrpheusBuffer* inputs[] = {&in};
    OrpheusBuffer* outputs[] = {&out};
    OrpheusProcessContext context = {.inputs=inputs, .outputs=outputs, .input_count=1, .output_count=1};
    OrpheusValue value = {.type=ORPHEUS_VALUE_INT}, actual;
    CHECK(iface->create(&instance, &config) == ORPHEUS_OK && instance == &state);
    CHECK(iface->prepare(instance, &config) == ORPHEUS_OK);
    OrpheusRegistry registry = {.ctx=NULL, .add=check_slot};
    CHECK(iface->register_slots(instance, &registry) == ORPHEUS_OK && slots_seen == 1);
    for (unsigned pass = 0; pass < 2; ++pass) {
        unsigned cursor = 0;
        if (pass) {
            CHECK(iface->reset(instance) == ORPHEUS_OK);
            CHECK(iface->get_parameter(instance, "delay_samples", &actual) == ORPHEUS_OK);
            CHECK(actual.value.i32 == -1);
            memset(history, 0, sizeof(history));
        }
        for (unsigned block = 0; block < 64; ++block) {
            unsigned frames = block % 2 ? 3 : 7;
            int delay = block < 8 ? 5 : block < 16 ? 0 : block < 24 ? 16
                      : block < 32 ? 1 : -1;
            value.value.i32 = delay;
            CHECK(iface->set_parameter(instance, "delay_samples", &value) == ORPHEUS_OK);
            for (unsigned n = 0; n < frames; ++n)
                for (unsigned c = 0; c < 3; ++c)
                    history[cursor+n][c] = input[n*3+c] = (float)(cursor+n+1) * (float)(c+1);
            context.frame_count = in.frame_count = frames;
            CHECK(iface->process(instance, &context) == ORPHEUS_OK);
            CHECK(out.frame_count == frames);
            for (unsigned n = 0; n < frames; ++n)
                for (unsigned c = 0; c < 3; ++c) {
                    unsigned d = delay < 0 ? legacy[c] : (unsigned)delay;
                    float expected = cursor+n >= d ? history[cursor+n-d][c] : 0.0f;
                    CHECK(output[n*3+c] == expected);
                }
            cursor += frames;
        }
        value.value.i32 = 17;
        CHECK(iface->set_parameter(instance, "delay_samples", &value) == ORPHEUS_ERR_INVALID_ARG);
        value.value.i32 = -2;
        CHECK(iface->set_parameter(instance, "delay_samples", &value) == ORPHEUS_ERR_INVALID_ARG);
        value.type = ORPHEUS_VALUE_FLOAT; value.value.f32 = 1.0f;
        CHECK(iface->set_parameter(instance, "delay_samples", &value) == ORPHEUS_ERR_INVALID_ARG);
        CHECK(iface->get_parameter(instance, "delay_samples", &actual) == ORPHEUS_OK);
        CHECK(actual.value.i32 == -1);
        value.type = ORPHEUS_VALUE_INT;
    }
    value.value.i32 = 5;
    CHECK(iface->set_parameter(instance, "delay_samples", &value) == ORPHEUS_OK);
    CHECK(iface->reset(instance) == ORPHEUS_OK);
    CHECK(iface->get_parameter(instance, "delay_samples", &actual) == ORPHEUS_OK);
    CHECK(actual.value.i32 == 5);
    context.frame_count = in.frame_count = 7;
    CHECK(iface->process(instance, &context) == ORPHEUS_OK);
    for (unsigned n = 0; n < 7; ++n)
        for (unsigned c = 0; c < 3; ++c)
            CHECK(output[n*3+c] == (n < 5 ? 0.0f : input[(n-5)*3+c]));
    CHECK(iface->destroy(instance) == ORPHEUS_OK);
    orpheus_builtin_delay_line_State invalid = {0};
    config.state_block=&invalid;
    values[3].value.i32 = 17;
    CHECK(iface->create(&instance, &config) == ORPHEUS_ERR_INVALID_ARG);
    values[3].type = ORPHEUS_VALUE_FLOAT;
    CHECK(iface->create(&instance, &config) == ORPHEUS_ERR_INVALID_ARG);
    config.param_count = 3;
    CHECK(iface->create(&instance, &config) == ORPHEUS_OK);
    CHECK(iface->prepare(&invalid, &config) == ORPHEUS_OK);
    CHECK(invalid.delay_samples == -1);
    CHECK(iface->destroy(&invalid) == ORPHEUS_OK);
    CHECK(test_integer_vector(iface) == 0);
    CHECK(test_large_history(iface) == 0);
    return 0;
}
