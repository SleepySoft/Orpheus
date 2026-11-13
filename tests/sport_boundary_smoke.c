#include "component_state.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); return 1; } } while (0)
#if SPORT_INPUT
typedef orpheus_builtin_sport_tdm_in_State State;
#else
typedef orpheus_builtin_sport_tdm_out_State State;
#endif
extern const OrpheusComponentInterface* orpheus_get_interface(void);
static State state, snapshot;
static int slots_seen;
static OrpheusSlotId check_slot(void* user, const OrpheusSlotInfo* slot) {
    (void)user;
    if (!strcmp(slot->key, "samples") && slot->count == 12 &&
        slot->type == ORPHEUS_VALUE_FLOAT && (slot->flags & ORPHEUS_SLOT_DOUBLE_BUFFERED))
        ++slots_seen;
    return 0;
}
#if SPORT_INPUT
static int calls;
static int deliver(void* user, uint32_t port, const OrpheusDelivery* delivery) {
    (void)user;
    ++calls;
    return port == 1 && delivery && delivery->frame_index == 11 && delivery->epoch == 3 &&
        (!delivery->buffer || delivery->buffer->frame_count == 4) ? -77 : -99;
}
#endif

int main(void) {
    const OrpheusComponentInterface* api = orpheus_get_interface();
    const OrpheusComponentDescriptor* descriptor = api->get_descriptor();
    CHECK(descriptor->port_count == 1);
    const char* ids[] = {"channels"};
    OrpheusValue channels = {.type = ORPHEUS_VALUE_INT, .value.i32 = 3};
    OrpheusConfig config = {.block_size = 4, .channels = 3, .sample_rate = 48000,
        .state_block = &state, .param_count = 1, .param_ids = ids, .param_values = &channels};
    void* instance = NULL;
    CHECK(api->create(&instance, &config) == 0 && api->prepare(instance, &config) == 0);
    OrpheusRegistry registry = {.add = check_slot};
    CHECK(api->register_slots(instance, &registry) == 0 && slots_seen == 1);
    float data[12];
    for (unsigned i = 0; i < 12; ++i) data[i] = (float)i - 3.5f;
    data[0] = -0.0f;
    OrpheusBuffer buffer = {.data = data, .channels = 3, .frame_count = 4,
        .frame_capacity = 4, .format = ORPHEUS_FORMAT_F32, .interleaved = true};
#if SPORT_INPUT
    OrpheusBuffer* buffers[] = {&buffer};
#else
    const OrpheusBuffer* buffers[] = {&buffer};
#endif
    OrpheusProcessContext ctx = {.sample_rate = 48000, .frame_count = 4,
        .frame_index = 11, .epoch = 3};
#if SPORT_INPUT
    CHECK(descriptor->ports[0].direction == ORPHEUS_PORT_OUTPUT);
    OrpheusContinuation continuation = {.emit = deliver};
    ctx.outputs = buffers;
    ctx.output_count = 1;
    ctx.trigger_input = UINT32_MAX;
    ctx.continuation = &continuation;
    CHECK(api->process(instance, &ctx) == ORPHEUS_ERR_PROCESSING && state.underruns == 1 && !calls);
    state.underruns = INT_MAX;
    CHECK(api->process(instance, &ctx) == ORPHEUS_ERR_PROCESSING && state.underruns == INT_MAX);
    memcpy(state.algorithm.data, data, sizeof(data));
    OrpheusValue ready = {.type = ORPHEUS_VALUE_BOOL, .value.b = true};
    CHECK(api->set_parameter(instance, "ready", &ready) == 0);
#else
    CHECK(descriptor->ports[0].direction == ORPHEUS_PORT_INPUT);
    ctx.inputs = buffers;
    ctx.input_count = 1;
    ctx.trigger_input = 0;
    CHECK(!state.valid && api->process(instance, &ctx) == 0 && state.valid);
    CHECK(!memcmp(state.algorithm.data, data, sizeof(data)));
    data[3] = 27.5f;
    CHECK(api->process(instance, &ctx) == 0 && state.algorithm.data[3] == 27.5f);
    ctx.trigger_input = UINT32_MAX;
    CHECK(api->process(instance, &ctx) == ORPHEUS_ERR_INVALID_ARG);
    ctx.trigger_input = 0;
#endif
    snapshot = state;
    buffer.data = state.algorithm.data + 1;
    CHECK(api->process(instance, &ctx) == ORPHEUS_ERR_INVALID_ARG && !memcmp(&state, &snapshot, sizeof(state)));
    buffer.data = data;
    buffer.frame_count = buffer.frame_capacity = 3;
    CHECK(api->process(instance, &ctx) == ORPHEUS_ERR_INVALID_ARG && !memcmp(&state, &snapshot, sizeof(state)));
    buffer.frame_count = buffer.frame_capacity = 4;
    ctx.sample_rate = 44100;
    CHECK(api->process(instance, &ctx) == ORPHEUS_ERR_INVALID_ARG && !memcmp(&state, &snapshot, sizeof(state)));
    ctx.sample_rate = 48000;
    ctx.frame_count = 0;
    CHECK(api->process(instance, &ctx) == 0 && !memcmp(&state, &snapshot, sizeof(state)));
    ctx.frame_count = 4;
#if SPORT_INPUT
    CHECK(api->process(instance, &ctx) == -77 && calls == 1 && !state.ready);
    CHECK(!memcmp(data, state.algorithm.data, sizeof(data)));
    CHECK(api->process(instance, &ctx) == ORPHEUS_ERR_PROCESSING && calls == 1);
    CHECK(api->set_parameter(instance, "ready", &ready) == 0);
    buffers[0] = NULL;
    CHECK(api->process(instance, &ctx) == -77 && calls == 2 && !state.ready);
    buffers[0] = &buffer;
    CHECK(api->set_parameter(instance, "underruns", &channels) == ORPHEUS_ERR_UNSUPPORTED);
#else
    buffers[0] = NULL;
    CHECK(api->process(instance, &ctx) == ORPHEUS_ERR_INVALID_ARG);
    buffers[0] = &buffer;
    CHECK(api->set_parameter(instance, "valid", &channels) == ORPHEUS_ERR_UNSUPPORTED);
#endif
    config.sample_rate = 44100;
    CHECK(api->prepare(instance, &config) == ORPHEUS_ERR_INVALID_ARG && !state._orpheus_cleanup_pending);
    config.sample_rate = 48000;
    CHECK(api->prepare(instance, &config) == 0);
    snapshot = state;
    channels.value.i32 = 32;
    config.block_size = 2049;
    CHECK(api->prepare(instance, &config) == ORPHEUS_ERR_INVALID_ARG && !memcmp(&state, &snapshot, sizeof(state)));
    config.block_size = 2048;
    CHECK(api->prepare(instance, &config) == 0 && state._orpheus_config_frames == 2048 && state.channels == 32);
    CHECK(api->reset(instance) == 0);
    for (unsigned i = 0; i < 65536; ++i) CHECK(state.algorithm.data[i] == 0);
#if SPORT_INPUT
    CHECK(!state.ready && !state.underruns);
#else
    CHECK(!state.valid);
#endif
    CHECK(api->destroy(instance) == 0);
    return 0;
}
