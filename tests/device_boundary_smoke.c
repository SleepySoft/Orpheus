#include "component_state.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); return 1; } } while (0)
#if DEVICE_INPUT
typedef orpheus_builtin_device_in_State State;
static uint64_t expected_index;
static uint32_t expected_frames;
static int calls;
static int deliver(void* user, uint32_t port, const OrpheusDelivery* delivery) {
    (void)user;
    ++calls;
    return port == 1 && delivery && delivery->frame_index == expected_index &&
        delivery->epoch == 3 && delivery->buffer && delivery->buffer->frame_count == expected_frames
        ? -77 : -99;
}
#else
typedef orpheus_builtin_device_out_State State;
#endif
extern const OrpheusComponentInterface* orpheus_get_interface(void);

int main(void) {
    State state, snapshot;
    void* instance = NULL;
    const OrpheusComponentInterface* api = orpheus_get_interface();
    const char* ids[] = {"channels"};
    OrpheusValue channels = {.type = ORPHEUS_VALUE_INT, .value.i32 = 3};
    OrpheusConfig config = {.state_block = &state, .channels = 3, .block_size = 4,
        .sample_rate = 48000, .param_count = 1, .param_ids = ids, .param_values = &channels};
    CHECK(api->create(&instance, &config) == 0 && api->prepare(instance, &config) == 0);
    OrpheusValue value = {.type = ORPHEUS_VALUE_INT};
    CHECK(api->get_parameter(instance, "device", &value) == 0 && value.type == ORPHEUS_VALUE_STRING &&
        !strcmp(value.value.str, ""));
    CHECK(api->get_parameter(instance, "sample_rate", &value) == 0 && value.value.i32 == 0);
#if DEVICE_INPUT
    CHECK(api->get_parameter(instance, "source", &value) == 0 && !strcmp(value.value.str, "microphone"));
#endif
    const uint32_t pattern[] = {0x80000000u, 0x7fc12345u, 0x7f800000u, 0xff800000u,
        0x00000001u, 0x3f800000u, 0x7f7fffffu, 0x00000000u, 0xbf800000u, 1, 2, 3};
    float data[12];
    memcpy(data, pattern, sizeof(data));
    OrpheusBuffer buffer = {.data = data, .channels = 3, .frame_count = 4, .frame_capacity = 4,
        .format = ORPHEUS_FORMAT_F32, .interleaved = true};
    OrpheusProcessContext ctx = {.sample_rate = 48000, .frame_count = 4, .epoch = 3, .frame_index = 999};
#if DEVICE_INPUT
    OrpheusBuffer* buffers[] = {&buffer};
    OrpheusContinuation continuation = {.emit = deliver};
    ctx.outputs = buffers;
    ctx.output_count = 1;
    ctx.trigger_input = UINT32_MAX;
    ctx.continuation = &continuation;
    expected_frames = 4;
    CHECK(api->process(instance, &ctx) == -77 && calls == 1 && state.algorithm.frame_index == 4);
    CHECK(!memcmp(data, pattern, sizeof(data)));
    expected_index = 4;
    expected_frames = 3;
    ctx.frame_count = 3;
    CHECK(api->process(instance, &ctx) == -77 && calls == 2 && state.algorithm.frame_index == 7);
    CHECK(!memcmp(data, pattern, sizeof(data)));
#else
    const OrpheusBuffer* buffers[] = {&buffer};
    ctx.inputs = buffers;
    ctx.input_count = 1;
    ctx.trigger_input = 0;
    CHECK(api->process(instance, &ctx) == 0 && !memcmp(data, pattern, sizeof(data)));
    ctx.frame_count = buffer.frame_count = 3;
    CHECK(api->process(instance, &ctx) == 0 && !memcmp(data, pattern, sizeof(data)));
    ctx.trigger_input = UINT32_MAX;
    CHECK(api->process(instance, &ctx) == ORPHEUS_ERR_INVALID_ARG);
    ctx.trigger_input = 0;
#endif
    snapshot = state;
    buffer.data = &state;
    CHECK(api->process(instance, &ctx) == ORPHEUS_ERR_INVALID_ARG && !memcmp(&state, &snapshot, sizeof(state)));
    buffer.data = data;
    ctx.sample_rate = 44100;
    CHECK(api->process(instance, &ctx) == ORPHEUS_ERR_INVALID_ARG && !memcmp(&state, &snapshot, sizeof(state)));
    ctx.sample_rate = 48000;
    buffer.frame_count = buffer.frame_capacity = 2;
    CHECK(api->process(instance, &ctx) == ORPHEUS_ERR_INVALID_ARG && !memcmp(&state, &snapshot, sizeof(state)));
    buffer.frame_count = buffer.frame_capacity = 4;
    ctx.frame_count = 0;
    CHECK(api->process(instance, &ctx) == 0 && !memcmp(&state, &snapshot, sizeof(state)));
    ctx.frame_count = 4;
    buffers[0] = NULL;
    CHECK(api->process(instance, &ctx) == ORPHEUS_ERR_INVALID_ARG && !memcmp(&state, &snapshot, sizeof(state)));
    buffers[0] = &buffer;
#if DEVICE_INPUT
    state.algorithm.frame_index = UINT64_MAX - 2;
    CHECK(api->process(instance, &ctx) == ORPHEUS_ERR_INVALID_ARG && calls == 2);
    CHECK(api->reset(instance) == 0 && state.algorithm.frame_index == 0 && state.algorithm.sample_rate == 48000);
    expected_index = 0;
    expected_frames = 4;
    CHECK(api->process(instance, &ctx) == -77 && calls == 3);
#endif
    CHECK(api->destroy(instance) == 0);
    return 0;
}
