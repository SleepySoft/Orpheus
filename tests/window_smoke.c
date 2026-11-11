#include "component_state.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); return 1; } } while (0)
static OrpheusSlotInfo coefficient_slot;
static OrpheusSlotId next_slot;
static unsigned calls;
static int error;
static OrpheusSlotId add(void* user, const OrpheusSlotInfo* info) {
    (void)user;
    if (!strcmp(info->key, "coefficients")) coefficient_slot=*info;
    return ++next_slot;
}
static int emit(void* user, uint32_t port, const OrpheusDelivery* delivery) {
    if (port != 1 || !delivery || delivery->buffer != user) return ORPHEUS_ERR_INVALID_ARG;
    ++calls;
    return error;
}

static int shape(const OrpheusComponentInterface* api, unsigned channels, unsigned size,
    unsigned frames, bool repeat, bool full) {
    const char* text[] = {"0", "-0", "1", "-1", "0.3", "inf", "nan", "1e-40"};
    const float weights[] = {0, -0.0f, 1, -1, .3f, INFINITY, NAN, 1e-40f};
    char csv[65536] = "";
    size_t cursor = 0;
    unsigned supplied = full ? size : 2;
    for (unsigned i = 0; i < supplied; ++i)
        cursor += (size_t)sprintf(csv+cursor, "%s%s", i ? "," : "", text[i%8]);
    size_t count = (size_t)frames * channels;
    float* original = malloc(count * sizeof(float));
    float* input = malloc((count+1) * sizeof(float));
    float* output = malloc(count * sizeof(float));
    CHECK(original && input && output);
    for (size_t i = 0; i < count; ++i) original[i] = ((int)(i%97)-48)*.03125f;
    const char* ids[] = {"window_size", "coefficients", "channels", "mode"};
    OrpheusValue params[] = {
        {.type=ORPHEUS_VALUE_INT, .value.i32=size}, {.type=ORPHEUS_VALUE_STRING, .value.str=csv},
        {.type=ORPHEUS_VALUE_INT, .value.i32=channels},
        {.type=ORPHEUS_VALUE_STRING, .value.str=repeat ? "repeat" : "single"}
    };
    orpheus_builtin_window_State state;
    void* handle=NULL;
    OrpheusConfig config = {.sample_rate=48000, .block_size=frames, .state_block=&state,
        .param_ids=ids, .param_values=params, .param_count=4};
    CHECK(api->create(&handle, &config) == 0 && handle == &state && api->prepare(handle, &config) == 0);
    OrpheusRegistry registry = {.add=add};
    CHECK(api->register_slots(handle, &registry) == 0);
    CHECK(coefficient_slot.kind == ORPHEUS_SLOT_BULK && coefficient_slot.type == ORPHEUS_VALUE_FLOAT
        && coefficient_slot.count == size && coefficient_slot.size == sizeof(float)
        && (coefficient_slot.flags & ORPHEUS_SLOT_DOUBLE_BUFFERED)
        && (char*)handle+coefficient_slot.offset == (char*)state.algorithm.coeffs);
    for (unsigned i = 0; i < size; ++i) {
        float want = i < supplied ? weights[i%8] : 0;
        CHECK(isnan(want) ? isnan(state.algorithm.coeffs[i])
            : !memcmp(&want, &state.algorithm.coeffs[i], sizeof(float)));
    }
    OrpheusBuffer in = {.data=input, .channels=channels, .frame_count=frames, .frame_capacity=frames,
        .format=ORPHEUS_FORMAT_F32, .interleaved=true};
    OrpheusBuffer out=in; out.data=output; out.frame_count=0;
    const OrpheusBuffer* inputs[] = {&in}; OrpheusBuffer* outputs[] = {&out};
    OrpheusContinuation continuation = {.emit=emit, .user=&out};
    OrpheusProcessContext ctx = {.inputs=inputs, .outputs=outputs, .input_count=1, .output_count=1,
        .frame_count=frames, .continuation=&continuation};
    for (unsigned pass = 0; pass < 3; ++pass) {
        memcpy(input, original, count*sizeof(float));
        unsigned actual = pass == 2 ? 1 : frames;
        in.frame_count=ctx.frame_count=actual; out.data=pass == 1 ? input : output;
        calls=0;
        CHECK(api->process(handle, &ctx) == 0 && calls == 1 && out.frame_count == actual);
        for (unsigned f = 0; f < actual; ++f) for (unsigned c = 0; c < channels; ++c) {
            unsigned index = repeat ? f%size : f;
            float weight = index >= size ? 1 : index < supplied ? weights[index%8] : 0;
            float want = original[(size_t)f*channels+c] * weight;
            float got = ((float*)out.data)[(size_t)f*channels+c];
            CHECK(isnan(want) ? isnan(got) : !memcmp(&want, &got, sizeof(float)));
        }
    }
    out.data=output; in.frame_count=ctx.frame_count=frames;
    memcpy(input, original, count*sizeof(float));
    error=-77; calls=0; CHECK(api->process(handle, &ctx) == -77 && calls == 1); error=0;
    orpheus_builtin_window_State saved=state;
    const char* invalid[] = {"oops", "1x", "1,", "1,,2"};
    for (unsigned i = 0; i < sizeof(invalid)/sizeof(*invalid); ++i) {
        params[1].value.str=invalid[i];
        CHECK(api->prepare(handle, &config) == ORPHEUS_ERR_INVALID_ARG && !memcmp(&state, &saved, sizeof(state)));
    }
    params[1].value.str=csv; params[0].value.i32=4097;
    CHECK(api->prepare(handle, &config) == ORPHEUS_ERR_INVALID_ARG && !memcmp(&state, &saved, sizeof(state)));
    params[0].value.i32=size; params[2].value.i32=33;
    CHECK(api->prepare(handle, &config) == ORPHEUS_ERR_INVALID_ARG);
    params[2].value.i32=channels; params[3].value.str="bad";
    CHECK(api->prepare(handle, &config) == ORPHEUS_ERR_INVALID_ARG);
    params[3].value.str=repeat ? "repeat" : "single";
    calls=0; in.frame_count=frames-1;
    CHECK(api->process(handle, &ctx) == ORPHEUS_ERR_INVALID_ARG && !calls);
    in.frame_count=frames; out.frame_capacity=frames-1;
    CHECK(api->process(handle, &ctx) == ORPHEUS_ERR_INVALID_ARG && !calls); out.frame_capacity=frames;
    if (count > 1) {
        out.data=input+1; CHECK(api->process(handle, &ctx) == ORPHEUS_ERR_INVALID_ARG && !calls);
    }
    out.data=output; ctx.frame_count=0;
    CHECK(api->process(handle, &ctx) == 0 && !calls); ctx.frame_count=frames;
    inputs[0]=NULL; CHECK(api->process(handle, &ctx) == ORPHEUS_ERR_INVALID_ARG); inputs[0]=&in;
    outputs[0]=NULL; CHECK(api->process(handle, &ctx) == ORPHEUS_ERR_INVALID_ARG); outputs[0]=&out;
    CHECK(api->reset(handle) == 0 && !strcmp(state.mode, "single"));
    CHECK(state.algorithm.coeffs[0] == 1);
    for (unsigned i = 1; i < 4096; ++i) CHECK(state.algorithm.coeffs[i] == 0);
    CHECK(api->process(handle, &ctx) == 0);
    for (unsigned f = 0; f < frames; ++f) for (unsigned c = 0; c < channels; ++c) {
        float want = input[(size_t)f*channels+c] * (f == 0 || f >= size ? 1.0f : 0.0f);
        CHECK(!memcmp(&want, output+(size_t)f*channels+c, sizeof(float)));
    }
    params[1].value.str=" \t\n";
    CHECK(api->prepare(handle, &config) == 0 && state.algorithm.coeffs[0] == 1);
    for (unsigned i = 1; i < size; ++i) CHECK(state.algorithm.coeffs[i] == 0);
    params[1].value.str=" 0.25\t-0.5\n 1  ";
    if (size >= 3) CHECK(api->prepare(handle, &config) == 0
        && state.algorithm.coeffs[0] == .25f && state.algorithm.coeffs[1] == -.5f && state.algorithm.coeffs[2] == 1);
    else CHECK(api->prepare(handle, &config) == ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->destroy(handle) == 0);
    config.state_block=NULL; CHECK(api->create(&handle, &config) == ORPHEUS_ERR_INVALID_ARG);
    free(original); free(input); free(output);
    return 0;
}

int main(void) {
    const OrpheusComponentInterface* api=orpheus_get_interface();
    CHECK(shape(api, 1, 2, 1, false, false) == 0);
    CHECK(shape(api, 3, 4, 11, false, false) == 0);
    CHECK(shape(api, 3, 4, 11, true, true) == 0);
    CHECK(shape(api, 32, 4096, 8192, false, true) == 0);
    CHECK(shape(api, 32, 4096, 8193, true, true) == 0);
    return 0;
}
