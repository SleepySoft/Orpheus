#include "component_state.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%d: %s\n", __LINE__, #expr); return 1; } } while (0)
typedef orpheus_builtin_iir_bank_State IirBankState;
typedef orpheus_builtin_iir_bank_ChannelState IirChannelState;
#define IIR_BANK_MAX_CHANNELS 64

static int create(const OrpheusComponentInterface* api, IirBankState* state, OrpheusConfig* config) {
    config->state_block=state;
    void* handle=NULL;
    return api->create(&handle,config);
}

typedef struct { unsigned calls,bulk; int failure; OrpheusSlotInfo slot; } Capture;
static OrpheusSlotId add(void* user, const OrpheusSlotInfo* slot) {
    Capture* capture=user;
    if (slot->kind==ORPHEUS_SLOT_BULK) { capture->slot=*slot; capture->bulk++; }
    return capture->failure ? ORPHEUS_SLOT_ID_INVALID : 0;
}
static int emit(void* user, uint32_t port, const OrpheusDelivery* delivery) {
    Capture* capture=user;
    if (port!=1 || !delivery || !delivery->buffer || delivery->frame_index!=777 ||
        delivery->epoch!=9 || delivery->buffer->frame_count!=3) return ORPHEUS_ERR_INVALID_ARG;
    capture->calls++;
    return capture->failure;
}

static int check_delivery(const OrpheusComponentInterface* api) {
    IirBankState state={0};
    const char* ids[]={"num_stages","coefs","coefs_mode"};
    OrpheusValue values[]={{.type=ORPHEUS_VALUE_INT,.value.i32=16},
        {.type=ORPHEUS_VALUE_STRING,.value.str="0.5,0.25,0,-0.5,0"},
        {.type=ORPHEUS_VALUE_STRING,.value.str="shared"}};
    OrpheusConfig config={.channels=64,.param_count=3,.param_ids=ids,.param_values=values};
    CHECK(create(api,&state,&config)==0 && api->prepare(&state,&config)==0);
    Capture capture={0};OrpheusRegistry registry={.ctx=&capture,.add=add};
    CHECK(api->register_slots(&state,&registry)==0 && capture.bulk==1 &&
        !strcmp(capture.slot.key,"coefs") && capture.slot.count==80 &&
        capture.slot.offset==offsetof(IirBankState,algorithm.shared) &&
        (capture.slot.flags & ORPHEUS_SLOT_DOUBLE_BUFFERED));
    capture.failure=ORPHEUS_ERR_PROCESSING;
    CHECK(api->register_slots(&state,&registry)==ORPHEUS_ERR_PROCESSING);
    capture.failure=0;
    float input[3*64],output[3*64];
    for (unsigned n=0;n<3*64;++n) input[n]=(float)((int)(n%11)-5)*.125f;
    OrpheusBuffer in={.data=input,.channels=64,.format=ORPHEUS_FORMAT_F32,.interleaved=true,
        .frame_count=3,.frame_capacity=3},out=in;
    out.data=output;
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&out};
    OrpheusContinuation continuation={.user=&capture,.emit=emit};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,
        .frame_count=3,.frame_index=777,.epoch=9,.valid_frames=3,.continuation=&continuation};
    float z1[64]={0},z2[64]={0};
    for (unsigned pass=0;pass<2;++pass) {
        capture.failure=pass ? 0 : ORPHEUS_ERR_PROCESSING;
        CHECK(api->process(&state,&ctx)==capture.failure && capture.calls==pass+1);
        for (unsigned n=0;n<3;++n) for (unsigned c=0;c<64;++c) {
            float x=input[n*64+c],y=.5f*x+z1[c];
            z1[c]=.25f*x+z2[c]-(-.5f*y);z2[c]=0.0f*x-0.0f*y;
            CHECK(!memcmp(output+n*64+c,&y,sizeof(float)));
        }
    }
    orpheus_builtin_iir_bank_AlgorithmState saved=state.algorithm;
    const char* invalid[]={"0.5,","abc","1,0x","1e9999","1,,0"};
    for (unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
        values[1].value.str=invalid[i];
        CHECK(api->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG &&
            !memcmp(&saved,&state.algorithm,sizeof(saved)));
    }
    values[1].value.str="";values[2].value.str="bad_mode";
    CHECK(api->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG &&
        !memcmp(&saved,&state.algorithm,sizeof(saved)));
    in.frame_count=2;
    CHECK(api->process(&state,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    in.frame_count=3;out.data=input;
    CHECK(api->process(&state,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    out.data=state.algorithm.shared;
    CHECK(api->process(&state,&ctx)==ORPHEUS_ERR_INVALID_ARG &&
        !memcmp(&saved,&state.algorithm,sizeof(saved)) && capture.calls==2);
    ctx.frame_count=0;outputs[0]=NULL;
    CHECK(api->process(&state,&ctx)==0 && capture.calls==2);
    values[2].value.str="per_channel";
    CHECK(api->prepare(&state,&config)==0);
    capture.bulk=0;
    CHECK(api->register_slots(&state,&registry)==0 && capture.bulk==1 &&
        !strcmp(capture.slot.key,"coefs_per_channel") && capture.slot.count==64*80 &&
        capture.slot.offset==offsetof(IirBankState,algorithm.per_channel));
    values[2].value.str="shared";values[1].value.str="1.401298464e-45,0,0,0,0";
    CHECK(api->prepare(&state,&config)==0);
    uint32_t smallest=1;
    CHECK(!memcmp(state.algorithm.shared,&smallest,sizeof(smallest)));
    return api->destroy(&state);
}

static int check_stage_counts(const OrpheusComponentInterface* api) {
    IirBankState state = {0};
    const char* keys[] = {"num_stages", "stages_per_channel", "coefs_mode"};
    OrpheusValue values[] = {
        {.type = ORPHEUS_VALUE_INT, .value.i32 = 3},
        {.type = ORPHEUS_VALUE_STRING, .value.str = " 0 , 1 , 3 \t"},
        {.type = ORPHEUS_VALUE_STRING, .value.str = "per_channel"}
    };
    OrpheusConfig config = {.channels = 3, .param_count = 3, .param_ids = keys, .param_values = values};
    CHECK(create(api,&state,&config)==ORPHEUS_OK);
    CHECK(api->prepare(&state, &config) == ORPHEUS_OK);
    for (unsigned c = 0; c < 3; ++c) {
        for (unsigned stage = 0; stage < 3; ++stage) {
            if (stage < state.algorithm.channelStages[c]) {
                state.algorithm.per_channel[c*80+5*stage] = 2;
            } else {
                for (unsigned k = 0; k < 5; ++k) state.algorithm.per_channel[c*80+5*stage+k] = NAN;
                state.algorithm.channelStates[c].z1[stage] = 123;
                state.algorithm.channelStates[c].z2[stage] = 456;
            }
        }
    }
    float input[] = {-0.0f, 1, 2}, output[3];
    OrpheusBuffer in = {.data=input,.channels=3,.format=ORPHEUS_FORMAT_F32,.interleaved=true,
        .frame_count=1,.frame_capacity=1}, out=in;
    out.data=output;
    const OrpheusBuffer* inputs[] = {&in};
    OrpheusBuffer* outputs[] = {&out};
    OrpheusProcessContext context = {.inputs=inputs,.outputs=outputs,.frame_count=1,.input_count=1,.output_count=1};
    CHECK(api->process(&state, &context) == ORPHEUS_OK);
    CHECK(output[0] == 0 && signbit(output[0]) && output[1] == 2 && output[2] == 16);
    CHECK(state.algorithm.channelStates[0].z1[0] == 123 && state.algorithm.channelStates[1].z2[2] == 456);
    CHECK(api->reset(&state) == ORPHEUS_OK);
    CHECK(state.algorithm.channelStages[0] == 0 && state.algorithm.channelStages[1] == 1 &&
        state.algorithm.channelStages[2] == 3);
    const char* invalid[] = {"0,1", "0,1,3,0", "0,1,3,", "0,,3", "0,1,4", "-1,1,3",
                             "0,1,1.5", "  ", "999999999999999999999999999999,1,3", "0,1,3x"};
    for (unsigned n = 0; n < sizeof(invalid)/sizeof(invalid[0]); ++n) {
        values[1].value.str = invalid[n];
        CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    }
    values[1].type = ORPHEUS_VALUE_INT;
    CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    values[1].type = ORPHEUS_VALUE_STRING; values[1].value.str = "";
    CHECK(api->prepare(&state, &config) == ORPHEUS_OK);
    for (unsigned c = 0; c < 3; ++c) CHECK(state.algorithm.channelStages[c] == 3);
    return api->destroy(&state);
}

static int check_wide_channels(const OrpheusComponentInterface* api) {
    IirBankState state = {0};
    const char* keys[] = {"num_stages", "coefs_mode"};
    OrpheusValue values[] = {
        {.type = ORPHEUS_VALUE_INT, .value.i32 = 1},
        {.type = ORPHEUS_VALUE_STRING, .value.str = "per_channel"}
    };
    OrpheusConfig config = {.channels = 64, .param_count = 2, .param_ids = keys, .param_values = values};
    CHECK(create(api,&state,&config)==ORPHEUS_OK);
    CHECK(api->prepare(&state, &config) == ORPHEUS_OK);
    float input[64], output[64];
    for (unsigned c = 0; c < 64; ++c) {
        input[c] = (float)(c+1);
        output[c] = NAN;
        state.algorithm.per_channel[c*80] = (float)(c+1)/64;
    }
    OrpheusBuffer in = {.data=input,.channels=64,.format=ORPHEUS_FORMAT_F32,.interleaved=true,
        .frame_count=1,.frame_capacity=1}, out=in;
    out.data=output;
    const OrpheusBuffer* inputs[] = {&in};
    OrpheusBuffer* outputs[] = {&out};
    OrpheusProcessContext context = {.inputs=inputs,.outputs=outputs,.frame_count=1,.input_count=1,.output_count=1};
    CHECK(api->process(&state, &context) == ORPHEUS_OK);
    for (unsigned c = 0; c < 64; ++c) CHECK(output[c] == (float)((c+1)*(c+1))/64);
    config.channels = 65;
    CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    return api->destroy(&state);
}

int main(void) {
    const OrpheusComponentInterface* api = orpheus_get_interface();
    IirBankState state = {0};
    const char* keys[] = {"num_stages", "coefs"};
    OrpheusValue values[] = {
        {.type = ORPHEUS_VALUE_INT, .value.i32 = 1},
        {.type = ORPHEUS_VALUE_STRING, .value.str = "0.5,0.25,0,-0.5,0"}
    };
    OrpheusConfig config = {.channels = 2, .sample_rate = 48000, .block_size = 32,
        .param_count = 2, .param_ids = keys, .param_values = values};
    CHECK(create(api,&state,&config)==ORPHEUS_OK);
    CHECK(api->prepare(&state, &config) == ORPHEUS_OK && state.processing_enabled == 1);
    float input[14] = {1, 2, 0, 0, 0, 0}, output[14];
    OrpheusBuffer in = {.data=input,.channels=2,.format=ORPHEUS_FORMAT_F32,.interleaved=true,
        .frame_count=7,.frame_capacity=7}, out=in;
    out.data=output;
    const OrpheusBuffer* inputs[] = {&in};
    OrpheusBuffer* outputs[] = {&out};
    OrpheusProcessContext context = {.inputs=inputs,.outputs=outputs,.frame_count=3,.input_count=1,.output_count=1};
    CHECK(api->process(&state, &context) == ORPHEUS_OK);
    IirChannelState frozen[IIR_BANK_MAX_CHANNELS];
    memcpy(frozen, state.algorithm.channelStates, sizeof(frozen));
    OrpheusValue enabled = {.type = ORPHEUS_VALUE_INT, .value.i32 = 0};
    CHECK(api->set_parameter(&state, "processing_enabled", &enabled) == ORPHEUS_OK);
    context.frame_count = 7;
    for (unsigned n = 0; n < 14; ++n) input[n] = n % 2 ? INFINITY : NAN;
    CHECK(api->process(&state, &context) == ORPHEUS_OK && out.frame_count == 7);
    for (unsigned n = 0; n < 14; ++n) CHECK(output[n] == 0 && !signbit(output[n]));
    CHECK(memcmp(frozen, state.algorithm.channelStates, sizeof(frozen)) == 0);
    inputs[0] = NULL;
    CHECK(api->process(&state, &context) == ORPHEUS_OK);
    enabled.value.i32 = 2;
    CHECK(api->set_parameter(&state, "processing_enabled", &enabled) == ORPHEUS_ERR_INVALID_ARG);
    enabled.type = ORPHEUS_VALUE_FLOAT;
    CHECK(api->set_parameter(&state, "processing_enabled", &enabled) == ORPHEUS_ERR_INVALID_ARG);
    CHECK(state.processing_enabled == 0);
    enabled.type = ORPHEUS_VALUE_INT; enabled.value.i32 = 1;
    CHECK(api->set_parameter(&state, "processing_enabled", &enabled) == ORPHEUS_OK);
    CHECK(api->process(&state, &context) == ORPHEUS_ERR_INVALID_ARG);
    inputs[0] = &in;
    memset(input, 0, sizeof(input));
    context.frame_count = 1;
    CHECK(api->process(&state, &context) == ORPHEUS_OK);
    CHECK(output[0] == frozen[0].z1[0] && output[1] == frozen[1].z1[0]);
    enabled.value.i32 = 0;
    CHECK(api->set_parameter(&state, "processing_enabled", &enabled) == ORPHEUS_OK);
    CHECK(api->reset(&state) == ORPHEUS_OK && state.processing_enabled == 0);
    memset(frozen, 0, sizeof(frozen));
    CHECK(memcmp(frozen, state.algorithm.channelStates, sizeof(frozen)) == 0 && state.algorithm.shared[0] == 0.5f);
    const char* disabled_keys[] = {"processing_enabled"};
    OrpheusValue disabled = {.type = ORPHEUS_VALUE_INT, .value.i32 = 0};
    config.param_ids = disabled_keys; config.param_values = &disabled; config.param_count = 1;
    CHECK(api->prepare(&state, &config) == ORPHEUS_OK && state.processing_enabled == 0);
    disabled.value.i32 = -1;
    CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->destroy(&state)==ORPHEUS_OK);
    CHECK(check_stage_counts(api) == 0);
    CHECK(check_delivery(api)==0);
    return check_wide_channels(api);
}
