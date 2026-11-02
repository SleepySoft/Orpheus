#include "orpheus_n_way_mux.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "检查失败，行%d: %s\n", __LINE__, #expr); return 1; \
} } while (0)

static int immediate_selection(void) {
    const OrpheusComponentInterface* iface = orpheus_get_interface();
    NWayMuxState state = {0};
    const char* ids[] = {"inputs", "select", "ramp_ms"};
    OrpheusValue values[] = {
        {.type=ORPHEUS_VALUE_INT, .value.i32=2},
        {.type=ORPHEUS_VALUE_INT, .value.i32=2},
        {.type=ORPHEUS_VALUE_FLOAT, .value.f32=0.0f}
    };
    OrpheusConfig config = {.channels=10, .sample_rate=48000, .block_size=32,
        .param_count=3, .param_ids=ids, .param_values=values};
    CHECK(iface->prepare(&state, &config) == ORPHEUS_OK);
    float a[320], b[320], output[320];
    for (unsigned i = 0; i < 320; ++i) {
        a[i] = i % 3 == 0 ? -0.0f : (float)((int)i - 160) / 512.0f;
        b[i] = NAN;
    }
    OrpheusBuffer ba = {.data=a, .channels=10, .format=ORPHEUS_FORMAT_F32,
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
    CHECK(memcmp(a, output, sizeof(a)) == 0);
    select.value.i32 = 2;
    CHECK(iface->set_parameter(&state, "select", &select) == ORPHEUS_OK);
    CHECK(iface->reset(&state) == ORPHEUS_OK);
    CHECK(iface->process(&state, &ctx) == ORPHEUS_OK);
    CHECK(memcmp(b, output, sizeof(b)) == 0);
    select.value.i32 = 1;
    CHECK(iface->set_parameter(&state, "select", &select) == ORPHEUS_OK);
    CHECK(iface->reset(&state) == ORPHEUS_OK);
    CHECK(iface->process(&state, &ctx) == ORPHEUS_OK);
    CHECK(memcmp(a, output, sizeof(a)) == 0);
    select.value.i32 = 2;
    CHECK(iface->set_parameter(&state, "select", &select) == ORPHEUS_OK);
    inputs[1] = NULL;
    CHECK(iface->process(&state, &ctx) == ORPHEUS_OK);
    for (unsigned i = 0; i < 320; ++i) CHECK(output[i] == 0.0f);
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
    CHECK(immediate_selection() == 0);
    CHECK(fading_selection() == 0);
    return 0;
}
