#include "orpheus_channel_selector.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); return 1; } } while (0)
static int slots_seen;
static OrpheusSlotId check_slot(void* ctx, const OrpheusSlotInfo* slot) {
    (void)ctx;
    if (slot->kind == ORPHEUS_SLOT_BULK) {
        if (slot->count == 6 && slot->size == 4 && (slot->flags & ORPHEUS_SLOT_DOUBLE_BUFFERED) &&
            ((strcmp(slot->key, "indices") == 0 && slot->type == ORPHEUS_VALUE_INT) ||
             (strcmp(slot->key, "gains") == 0 && slot->type == ORPHEUS_VALUE_FLOAT))) ++slots_seen;
    }
    return 0;
}
int main(void) {
    const OrpheusComponentInterface* iface = orpheus_get_interface();
    const char* keys[] = {"channels_a", "channels_b", "channels_out", "index_base", "high_index_policy", "apply_gain", "indices", "gains"};
    OrpheusValue values[] = {
        {.type=ORPHEUS_VALUE_INT, .value.i32=3}, {.type=ORPHEUS_VALUE_INT, .value.i32=2},
        {.type=ORPHEUS_VALUE_INT, .value.i32=6}, {.type=ORPHEUS_VALUE_INT, .value.i32=1},
        {.type=ORPHEUS_VALUE_INT, .value.i32=1}, {.type=ORPHEUS_VALUE_BOOL, .value.b=false},
        {.type=ORPHEUS_VALUE_STRING, .value.str="-2147483648,0,1,3,4,2147483647"},
        {.type=ORPHEUS_VALUE_STRING, .value.str="1,2,-1,0,0.5,-0"}
    };
    ChannelSelectorState state = {0};
    OrpheusConfig config = {.sample_rate=48000, .block_size=2, .param_ids=keys,
        .param_values=values, .param_count=8, .state_block=&state};
    void* instance = NULL;
    CHECK(iface->create(&instance, &config) == ORPHEUS_OK && instance == &state);
    CHECK(iface->prepare(instance, &config) == ORPHEUS_OK);
    OrpheusRegistry registry = {.add=check_slot};
    CHECK(iface->register_slots(instance, &registry) == ORPHEUS_OK && slots_seen == 2);
    uint32_t raw[] = {0x80000000u, 0x7f800000u, 0x7fc12345u, 0x00000000u, 0xff800000u, 0x3f800000u};
    float a[6], b[] = {2, -3, 4, -5}, output[12];
    memcpy(a, raw, sizeof(a));
    OrpheusBuffer ba = {.data=a, .channels=3, .format=ORPHEUS_FORMAT_F32, .frame_capacity=2, .interleaved=true};
    OrpheusBuffer bb = {.data=b, .channels=2, .format=ORPHEUS_FORMAT_F32, .frame_capacity=2, .interleaved=true};
    OrpheusBuffer bo = {.data=output, .channels=6, .format=ORPHEUS_FORMAT_F32, .frame_capacity=2, .interleaved=true};
    const OrpheusBuffer* inputs[] = {&ba, &bb, NULL};
    OrpheusBuffer* outputs[] = {&bo};
    OrpheusProcessContext context = {.inputs=inputs, .outputs=outputs, .input_count=2, .output_count=1, .frame_count=2};
    CHECK(iface->process(instance, &context) == ORPHEUS_OK);
    for (unsigned n = 0; n < 2; ++n) {
        float zero = 0;
        CHECK(!memcmp(&output[n*6], &zero, 4) && !memcmp(&output[n*6+1], &zero, 4));
        CHECK(!memcmp(&output[n*6+2], &a[n*3], 4));
        CHECK(!memcmp(&output[n*6+3], &a[n*3+2], 4));
        CHECK(output[n*6+4] == b[n*2] && output[n*6+5] == b[n*2+1]);
    }
    state.apply_gain = true;
    state.gains[0] = state.gains[1] = NAN;
    CHECK(iface->process(instance, &context) == ORPHEUS_OK);
    CHECK(output[0] == 0 && !signbit(output[0]) && output[1] == 0 && !signbit(output[1]));
    CHECK(!signbit(output[2]) && output[2] == 0 && isnan(output[3]) && output[4] == 1);
    CHECK(output[5] == 0 && !signbit(output[5]));
    inputs[1] = NULL;
    CHECK(iface->process(instance, &context) == ORPHEUS_OK);
    CHECK(output[4] == 0 && !signbit(output[4]) && output[5] == 0 && !signbit(output[5]));
    state.high_index_policy = 0;
    CHECK(iface->reset(instance) == ORPHEUS_OK && state.indices[5] == INT32_MAX && isnan(state.gains[0]));
    CHECK(iface->process(instance, &context) == ORPHEUS_OK && !signbit(output[5]));
    bo.frame_capacity = 1;
    CHECK(iface->process(instance, &context) == ORPHEUS_ERR_INVALID_ARG);
    bo.frame_capacity = 2;
    float base[12];
    for (unsigned i = 0; i < 12; ++i) memcpy(&base[i], &raw[i%6], 4);
    OrpheusBuffer base_buffer = {.data=base, .channels=6, .format=ORPHEUS_FORMAT_F32, .frame_capacity=2};
    inputs[2] = &base_buffer; context.input_count = 3; state.accumulate = true;
    OrpheusValue enabled = {.type=ORPHEUS_VALUE_BOOL, .value.b=false}, readback;
    CHECK(iface->set_parameter(instance, "enabled", &enabled) == ORPHEUS_OK);
    CHECK(iface->get_parameter(instance, "enabled", &readback) == ORPHEUS_OK && !readback.value.b);
    CHECK(iface->process(instance, &context) == ORPHEUS_OK && !memcmp(output, base, sizeof(base)));
    CHECK(iface->reset(instance) == ORPHEUS_OK && !state.enabled);
    enabled.value.b = true;
    CHECK(iface->set_parameter(instance, "enabled", &enabled) == ORPHEUS_OK);
    CHECK(iface->process(instance, &context) == ORPHEUS_OK);
    for (unsigned n = 0; n < 2; ++n) {
        CHECK(!memcmp(&output[n*6], &base[n*6], 4) && !memcmp(&output[n*6+1], &base[n*6+1], 4));
        CHECK(!memcmp(&output[n*6+4], &base[n*6+4], 4) && !memcmp(&output[n*6+5], &base[n*6+5], 4));
    }
    CHECK(iface->destroy(instance) == ORPHEUS_OK);
    const char* bad_indices[] = {"1,2", "1,2,3,4,5,6,7", "1.5,2,3,4,5,6", "1,,3,4,5,6",
        "1,2,3,4,5,6x", "1,2,3,4,5,", "2147483648,2,3,4,5,6"};
    for (unsigned i = 0; i < sizeof(bad_indices)/sizeof(bad_indices[0]); ++i) {
        values[6].value.str = bad_indices[i];
        CHECK(iface->prepare(instance, &config) == ORPHEUS_ERR_INVALID_ARG);
    }
    values[6].value.str = "";
    const char* bad_gains[] = {"1,2", "1,2,3,4,5,6,7", "1,2,3,4,5,inf", "1,2,3,4,5,nan", "1,2,3,4,5,6x"};
    for (unsigned i = 0; i < sizeof(bad_gains)/sizeof(bad_gains[0]); ++i) {
        values[7].value.str = bad_gains[i];
        CHECK(iface->prepare(instance, &config) == ORPHEUS_ERR_INVALID_ARG);
    }
    values[7].value.str = "";
    CHECK(iface->prepare(instance, &config) == ORPHEUS_OK && state.indices[0] == 1 && state.gains[0] == 1);
    values[0].value.i32 = 65;
    CHECK(iface->prepare(instance, &config) == ORPHEUS_ERR_INVALID_ARG);
    return 0;
}
