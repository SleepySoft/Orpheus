#include "orpheus_n_way_mux.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "检查失败，行%d: %s\n", __LINE__, #expr); return 1; \
} } while (0)

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
        .param_count=3, .param_ids=ids, .param_values=values};
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
        .param_count=3, .param_ids=ids, .param_values=values};
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
    return 0;
}

int main(void) {
    CHECK(immediate_selection(10) == 0);
    CHECK(immediate_selection(40) == 0);
    CHECK(immediate_selection(52) == 0);
    CHECK(immediate_selection(64) == 0);
    CHECK(fading_selection() == 0);
    return 0;
}
