#include "orpheus_buffer_source.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); return 1; } } while (0)
extern const OrpheusComponentInterface* orpheus_get_interface(void);
static int calls;
static int next(void* user, uint32_t branch) {
    (void)user;
    ++calls;
    return branch == UINT32_MAX ? -77 : -99;
}

int main(void) {
    const OrpheusComponentInterface* api = orpheus_get_interface();
    const OrpheusComponentDescriptor* descriptor = api->get_descriptor();
    CHECK(!strcmp(descriptor->id, "orpheus.builtin.buffer_source"));
    CHECK(descriptor->port_count == 1 && descriptor->ports[0].direction == ORPHEUS_PORT_OUTPUT);
    HostAudioState external;
    void* state = NULL;
    const char* ids[] = {"channels"};
    OrpheusValue channels = {.type=ORPHEUS_VALUE_INT, .value.i32=3};
    OrpheusConfig config = {.block_size=4, .channels=3, .state_block=&external,
        .param_count=1, .param_ids=ids, .param_values=&channels};
    CHECK(api->create(&state, &config) == 0 && api->prepare(state, &config) == 0);
    float data[12] = {0};
    OrpheusBuffer output = {.data=data, .channels=3, .frame_capacity=4,
        .format=ORPHEUS_FORMAT_F32, .interleaved=true};
    OrpheusBuffer* outputs[] = {&output};
    OrpheusContinuation continuation = {.call=next, .count=1};
    OrpheusProcessContext ctx = {.outputs=outputs, .output_count=1,
        .frame_count=4, .continuation=&continuation};
    CHECK(api->process(state, &ctx) == ORPHEUS_ERR_PROCESSING && calls == 0);
    for (unsigned i=0; i<12; ++i) external.data[i] = (float)i - 3.5f;
    OrpheusValue ready = {.type=ORPHEUS_VALUE_BOOL, .value.b=true};
    CHECK(api->set_parameter(state, "ready", &ready) == 0);
    output.frame_capacity = 3;
    CHECK(api->process(state, &ctx) == ORPHEUS_ERR_INVALID_ARG && external.ready);
    output.frame_capacity = 4;
    CHECK(api->process(state, &ctx) == -77 && calls == 1 && !external.ready);
    CHECK(output.frame_count == 4 && !memcmp(data, external.data, sizeof(data)));
    CHECK(api->process(state, &ctx) == ORPHEUS_ERR_PROCESSING && calls == 1);
    channels.value.i32 = 64;
    config.block_size = 129;
    CHECK(api->prepare(state, &config) == ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->reset(state) == 0 && !external.ready && external.missing_blocks == 0);
    CHECK(api->destroy(state) == 0);
    return 0;
}
