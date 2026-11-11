#include "component_state.h"

#ifdef calloc
#undef calloc
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%d: %s\n", __LINE__, #expr); return 1; } } while (0)
typedef orpheus_builtin_fir_bank_State FirBankState;

static int create(const OrpheusComponentInterface* api, FirBankState* state, OrpheusConfig* config) {
    void* handle=NULL;
    config->state_block=state;
    return api->create(&handle,config);
}

static OrpheusSlotInfo coefficient_slot;
static OrpheusSlotInfo index_slot;
static bool fail_allocation,fail_registration;
static unsigned float_slots,int_slots;

void* fir_bank_test_calloc(size_t count, size_t size) {
    return fail_allocation ? NULL : calloc(count,size);
}

static OrpheusSlotId capture(void* ctx, const OrpheusSlotInfo* slot) {
    (void)ctx;
    if (slot->kind == ORPHEUS_SLOT_BULK && slot->type == ORPHEUS_VALUE_FLOAT) {
        coefficient_slot = *slot;float_slots++;
    }
    if (slot->kind == ORPHEUS_SLOT_BULK && slot->type == ORPHEUS_VALUE_INT) {
        index_slot = *slot;int_slots++;
    }
    return fail_registration ? ORPHEUS_SLOT_ID_INVALID : 0;
}

typedef struct { unsigned calls; int error; } Delivery;
static int emit(void* user, uint32_t port, const OrpheusDelivery* delivery) {
    Delivery* result=user;
    if (port!=1 || !delivery || !delivery->buffer || delivery->frame_index!=333 || delivery->epoch!=8 ||
        delivery->buffer->frame_count!=3) return ORPHEUS_ERR_INVALID_ARG;
    result->calls++;
    return result->error;
}

static int errors_and_delivery(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    static FirBankState state;
    const char* ids[]={"num_taps","coefs"};
    OrpheusValue params[]={{.type=ORPHEUS_VALUE_INT,.value.i32=3},
        {.type=ORPHEUS_VALUE_STRING,.value.str="0.5,0.25,0.125"}};
    OrpheusConfig config={.channels=2,.param_count=2,.param_ids=ids,.param_values=params};
    CHECK(create(api,&state,&config)==0);
    fail_allocation=true;
    CHECK(api->prepare(&state,&config)==ORPHEUS_ERR_OUT_OF_MEMORY && !state.algorithm.history);
    fail_allocation=false;
    CHECK(api->prepare(&state,&config)==0);
    OrpheusRegistry registry={.add=capture};
    float_slots=int_slots=0;
    CHECK(api->register_slots(&state,&registry)==0 && float_slots==1 && int_slots==0 &&
        coefficient_slot.count==3 && !strcmp(coefficient_slot.key,"coefs"));
    fail_registration=true;
    CHECK(api->register_slots(&state,&registry)==ORPHEUS_ERR_PROCESSING);
    fail_registration=false;
    float input[7],output[6],seen[6][2];
    for (unsigned n=0;n<6;++n) for (unsigned c=0;c<2;++c)
        seen[n][c]=(float)((int)((n*7+c*3)%17)-8)*.125f;
    OrpheusBuffer in={.data=input,.channels=2,.format=ORPHEUS_FORMAT_F32,.interleaved=true,
        .frame_count=3,.frame_capacity=3},out=in;
    out.data=output;
    const OrpheusBuffer* inputs[]={&in};OrpheusBuffer* outputs[]={&out};
    Delivery result={0};OrpheusContinuation continuation={.user=&result,.emit=emit};
    OrpheusProcessContext ctx={.inputs=inputs,.outputs=outputs,.input_count=1,.output_count=1,
        .frame_count=3,.valid_frames=3,.frame_index=333,.epoch=8,.continuation=&continuation};
    const float coefs[]={.5f,.25f,.125f};
    for (unsigned pass=0;pass<2;++pass) {
        memcpy(input,seen+pass*3,6*sizeof(float));
        result.error=pass ? 0 : ORPHEUS_ERR_PROCESSING;
        CHECK(api->process(&state,&ctx)==result.error && result.calls==pass+1);
        for (unsigned n=0;n<3;++n) for (unsigned c=0;c<2;++c) {
            unsigned index=pass*3+n;
            float value=0;
            for (unsigned k=3;k>0;--k)
                value+=coefs[k-1]*(index+1<k ? 0.0f : seen[index+1-k][c]);
            CHECK(!memcmp(output+n*2+c,&value,sizeof(value)));
        }
    }
    float history[6];uint32_t positions[64];
    memcpy(history,state.algorithm.history,sizeof(history));
    memcpy(positions,state.algorithm.positions,sizeof(positions));
    float* allocated=state.algorithm.history;
    params[1].value.str="broken";
    CHECK(api->prepare(&state,&config)==ORPHEUS_ERR_INVALID_ARG && state.algorithm.history==allocated);
    in.frame_count=2;
    CHECK(api->process(&state,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    in.frame_count=3;out.data=input+1;
    CHECK(api->process(&state,&ctx)==ORPHEUS_ERR_INVALID_ARG);
    out.data=state.algorithm.history;
    CHECK(api->process(&state,&ctx)==ORPHEUS_ERR_INVALID_ARG && result.calls==2 &&
        !memcmp(history,state.algorithm.history,sizeof(history)) &&
        !memcmp(positions,state.algorithm.positions,sizeof(positions)));
    ctx.frame_count=0;outputs[0]=NULL;
    CHECK(api->process(&state,&ctx)==0 && result.calls==2);
    return api->destroy(&state);
}

static int bank_selection(void) {
    const OrpheusComponentInterface* api = orpheus_get_interface();
    static FirBankState state;
    const char* keys[] = {"num_taps", "num_filters", "coefs_mode", "coefs", "filter_indices"};
    OrpheusValue values[] = {
        {.type=ORPHEUS_VALUE_INT, .value.i32=3},
        {.type=ORPHEUS_VALUE_INT, .value.i32=2},
        {.type=ORPHEUS_VALUE_STRING, .value.str="banked"},
        {.type=ORPHEUS_VALUE_STRING, .value.str="1,0.5,0.25,2,1,0.5"},
        {.type=ORPHEUS_VALUE_STRING, .value.str="0,1"}
    };
    OrpheusConfig config = {.channels=2, .param_count=5, .param_ids=keys, .param_values=values};
    CHECK(create(api,&state,&config)==0);
    CHECK(api->prepare(&state, &config) == ORPHEUS_OK);
    OrpheusRegistry registry = {.add=capture};
    CHECK(api->register_slots(&state, &registry) == ORPHEUS_OK);
    CHECK(coefficient_slot.count == 6 && strcmp(coefficient_slot.key, "coefs_banked") == 0);
    CHECK(index_slot.count == 2 && index_slot.type == ORPHEUS_VALUE_INT && index_slot.max_i32 == 1);
    float data[2] = {1,2};
    OrpheusBuffer in={.data=data,.format=ORPHEUS_FORMAT_F32,.channels=2,.interleaved=true,
        .frame_count=1,.frame_capacity=1},out=in;
    const OrpheusBuffer* inputs[] = {&in};
    OrpheusBuffer* outputs[] = {&out};
    OrpheusProcessContext context = {.inputs=inputs,.outputs=outputs,.frame_count=1,.input_count=1,.output_count=1};
    CHECK(api->process(&state, &context) == ORPHEUS_OK && data[0] == 1 && data[1] == 4);
    state.filter_indices_i32[0] = 1; state.filter_indices_i32[1] = 0;
    OrpheusValue enable={.type=ORPHEUS_VALUE_INT,.value.i32=0};
    CHECK(api->set_parameter(&state,"processing_enabled",&enable)==0 &&
        state.filter_indices_i32[0]==1 && state.filter_indices_i32[1]==0);
    enable.value.i32=1;
    CHECK(api->set_parameter(&state,"processing_enabled",&enable)==0 &&
        state.filter_indices_i32[0]==1 && state.filter_indices_i32[1]==0);
    data[0] = 0; data[1] = 0;
    CHECK(api->process(&state, &context) == ORPHEUS_OK && data[0] == 1 && data[1] == 1);
    float history[6];
    memcpy(history, state.algorithm.history, sizeof(history));
    uint32_t positions[2];
    memcpy(positions, state.algorithm.positions, sizeof(positions));
    for (int bad = -1; bad <= 2; bad += 3) {
        state.filter_indices_i32[0] = bad;
        CHECK(api->process(&state, &context) == ORPHEUS_ERR_INVALID_ARG);
        CHECK(memcmp(history, state.algorithm.history, sizeof(history)) == 0);
        CHECK(memcmp(positions, state.algorithm.positions, sizeof(positions)) == 0);
    }
    state.filter_indices_i32[0] = 1;
    CHECK(api->reset(&state) == ORPHEUS_OK && state.filter_indices_i32[0] == 1 &&
        state.algorithm.coefficients.banked[0] == 1);
    state.filter_indices_i32[1] = 1; state.algorithm.coefficients.banked[0] = NAN;
    data[0] = 1; data[1] = 2;
    CHECK(api->process(&state, &context) == ORPHEUS_OK && data[0] == 2 && data[1] == 4);
    const char* bad_indices[] = {"-1,0", "0,2", "0", "0,1,", "0,,1", "0.5,1"};
    for (unsigned i = 0; i < sizeof(bad_indices)/sizeof(bad_indices[0]); ++i) {
        values[4].value.str = bad_indices[i];
        CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    }
    values[4].value.str = ""; values[3].value.str = "";
    values[1].value.i32 = 65;
    CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    values[1].value.i32 = 64; values[0].value.i32 = 513;
    CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->destroy(&state) == ORPHEUS_OK);
    return 0;
}

int main(void) {
    CHECK(errors_and_delivery()==0);
    CHECK(bank_selection() == 0);
    const OrpheusComponentInterface* api = orpheus_get_interface();
    static FirBankState state;
    const char* keys[] = {"num_taps", "coefs", "coefs_mode", "taps_per_channel"};
    OrpheusValue values[] = {
        {.type=ORPHEUS_VALUE_INT, .value.i32=3},
        {.type=ORPHEUS_VALUE_STRING, .value.str="1,0.5,0.25,2,99,99,99,99,99"},
        {.type=ORPHEUS_VALUE_STRING, .value.str="per_channel"},
        {.type=ORPHEUS_VALUE_STRING, .value.str="3,1,0"}
    };
    OrpheusConfig config = {.channels=3, .param_count=4, .param_ids=keys, .param_values=values};
    CHECK(create(api,&state,&config)==0);
    CHECK(api->prepare(&state, &config) == ORPHEUS_OK);
    OrpheusRegistry registry = {.add=capture};
    CHECK(api->register_slots(&state, &registry) == ORPHEUS_OK);
    CHECK(coefficient_slot.count == 9 && coefficient_slot.size == sizeof(float));
    CHECK(strcmp(coefficient_slot.key, "coefs_per_channel") == 0);
    CHECK(coefficient_slot.flags & ORPHEUS_SLOT_DOUBLE_BUFFERED);
    state.algorithm.coefficients.per_channel[4] = NAN;
    float data[9] = {1,2,NAN, 0,3,NAN, 0,4,NAN};
    OrpheusBuffer in={.data=data,.format=ORPHEUS_FORMAT_F32,.channels=3,.interleaved=true,
        .frame_count=3,.frame_capacity=3},out=in;
    const OrpheusBuffer* inputs[] = {&in};
    OrpheusBuffer* outputs[] = {&out};
    OrpheusProcessContext context = {.inputs=inputs,.outputs=outputs,.frame_count=3,.input_count=1,.output_count=1};
    CHECK(api->process(&state, &context) == ORPHEUS_OK);
    const float expected[] = {1,4,0, 0.5f,6,0, 0.25f,8,0};
    CHECK(memcmp(data, expected, sizeof(data)) == 0);
    float frozen[9];
    memcpy(frozen, state.algorithm.history, sizeof(frozen));
    uint32_t positions[3];
    memcpy(positions, state.algorithm.positions, sizeof(positions));
    OrpheusValue enable = {.type=ORPHEUS_VALUE_INT, .value.i32=0};
    CHECK(api->set_parameter(&state, "processing_enabled", &enable) == ORPHEUS_OK);
    inputs[0] = NULL;
    CHECK(api->process(&state, &context) == ORPHEUS_OK);
    for (unsigned i = 0; i < 9; ++i) CHECK(data[i] == 0 && !signbit(data[i]));
    CHECK(memcmp(frozen, state.algorithm.history, sizeof(frozen)) == 0);
    CHECK(memcmp(positions, state.algorithm.positions, sizeof(positions)) == 0);
    CHECK(api->reset(&state) == ORPHEUS_OK && state.processing_enabled == 0 &&
        state.algorithm.coefficients.per_channel[0] == 1);
    for (unsigned i = 0; i < 9; ++i) CHECK(state.algorithm.history[i] == 0);
    const char* invalid[] = {"1,,2", "1,2", "1,2,3,", "1,2,nan", "1,2,inf", "1,2,3oops"};
    values[2].value.str = "shared";
    for (unsigned i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
        values[1].value.str = invalid[i];
        CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    }
    values[1].value.str = "";
    const char* bad_lengths[] = {"1,2", "1,2,3,", "-1,1,1", "1,2,4", "1,1.5,2", "1,,2"};
    for (unsigned i = 0; i < sizeof(bad_lengths)/sizeof(bad_lengths[0]); ++i) {
        values[3].value.str = bad_lengths[i];
        CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    }
    values[3].value.str = "";
    values[1].value.str = "1e20,-1e20,1"; config.channels = 1;
    CHECK(api->prepare(&state, &config) == ORPHEUS_OK);
    float rounding[3] = {1,1,1};
    in.channels=out.channels=1;
    in.data = rounding; out.data = rounding; inputs[0] = &in;
    CHECK(api->process(&state, &context) == ORPHEUS_OK && rounding[2] == 0);
    values[1].value.str = "";
    values[0].value.i32 = 530; values[2].value.str = "per_channel"; config.channels = 42;
    CHECK(api->prepare(&state, &config) == ORPHEUS_OK);
    CHECK(api->register_slots(&state, &registry) == ORPHEUS_OK && coefficient_slot.count == 22260);
    float wide[64] = {0};
    in.channels=out.channels=42;in.frame_count=in.frame_capacity=out.frame_count=out.frame_capacity=1;
    wide[41] = 0.75f;
    in.data = wide; out.data = wide; inputs[0] = &in; context.frame_count = 1;
    CHECK(api->process(&state, &context) == ORPHEUS_OK && wide[41] == 0.75f);
    for (unsigned i = 0; i < 41; ++i) CHECK(wide[i] == 0);
    CHECK(api->reset(&state) == ORPHEUS_OK);
    state.algorithm.coefficients.per_channel[41*530+529] = 0.5f;
    for (unsigned frame = 0; frame < 533; ++frame) {
        memset(wide, 0, sizeof(wide));
        if (frame == 0) wide[41] = 0.75f;
        CHECK(api->process(&state, &context) == ORPHEUS_OK);
        CHECK(wide[41] == (frame == 0 ? 0.75f : frame == 529 ? 0.375f : 0));
        for (unsigned i = 0; i < 41; ++i) CHECK(wide[i] == 0);
    }
    values[0].value.i32 = 512; config.channels = 64;
    CHECK(api->prepare(&state, &config) == ORPHEUS_OK);
    values[0].value.i32 = 513;
    CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    values[0].value.i32 = 4096; values[2].value.str = "shared";
    CHECK(api->prepare(&state, &config) == ORPHEUS_OK);
    values[0].value.i32 = 4097;
    CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    values[0].value.i32 = 1; config.channels = 65;
    CHECK(api->prepare(&state, &config) == ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->destroy(&state) == ORPHEUS_OK);
    CHECK(api->destroy(&state) == ORPHEUS_OK);
    return 0;
}
