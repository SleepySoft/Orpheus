#include "component_state.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); return 1; } } while (0)
extern const OrpheusComponentInterface* orpheus_get_interface(void);
static int calls;
static int delivery_error = -77;
static int deliver(void* user, uint32_t port, const OrpheusDelivery* delivery) {
    (void)user;
    ++calls;
    return port == 1 && delivery && !delivery->buffer && delivery->frame_index == 11 &&
        delivery->epoch == 3 &&
        delivery->timeline_flags == ORPHEUS_TIMELINE_DISCONTINUITY ? delivery_error : -99;
}

int main(void) {
    const OrpheusComponentInterface* api = orpheus_get_interface();
    const OrpheusComponentDescriptor* desc = api->get_descriptor();
    CHECK(desc->port_count == 1 && desc->ports[0].type == ORPHEUS_PORT_TRIGGER &&
        desc->ports[0].direction == ORPHEUS_PORT_OUTPUT && desc->ports[0].channels == 0);
    orpheus_builtin_task_source_State external;
    OrpheusConfig config = {.state_block = &external, .sample_rate = 48000, .block_size = 480};
    void* state = NULL;
    CHECK(api->create(&state, &config) == 0 && api->prepare(state, &config) == 0);
    OrpheusValue frequency = {.type = ORPHEUS_VALUE_INT};
    CHECK(api->get_parameter(state, "frequency_hz", &frequency) == 0 && frequency.value.i32 == 100);
    OrpheusContinuation continuation = {.emit = deliver};
    OrpheusProcessContext ctx = {.continuation = &continuation, .frame_index = 11, .epoch = 3,
        .timeline_flags = ORPHEUS_TIMELINE_DISCONTINUITY, .trigger_input = UINT32_MAX};
    CHECK(api->process(state, &ctx) == -77 && calls == 1);
    CHECK(api->process(state, &ctx) == -77 && calls == 2);
    CHECK(api->reset(state) == 0);
    delivery_error = 0;
    CHECK(api->process(state, &ctx) == 0 && calls == 3);
    frequency.value.i32 = 200;
    CHECK(api->set_parameter(state, "frequency_hz", &frequency) == ORPHEUS_ERR_UNSUPPORTED);
    CHECK(api->get_parameter(state, "frequency_hz", &frequency) == 0 && frequency.value.i32 == 100);
    const char* ids[] = {"frequency_hz"};
    frequency.value.i32 = 0;
    config.param_ids = ids;
    config.param_values = &frequency;
    config.param_count = 1;
    orpheus_builtin_task_source_State snapshot = external;
    void* bad = state;
    CHECK(api->create(&bad, &config) == ORPHEUS_ERR_INVALID_ARG && !bad &&
        !memcmp(&external, &snapshot, sizeof(external)));
    CHECK(api->process(state, &ctx) == 0 && calls == 4);
    CHECK(api->destroy(state) == 0);
    return 0;
}
