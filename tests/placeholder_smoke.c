#include "orpheus_placeholder.h"

#include <stdio.h>
#include <string.h>

extern const OrpheusComponentInterface* orpheus_get_interface(void);

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "line %d: %s\n", __LINE__, #expr); return 1; } } while (0)

int main(void) {
    const OrpheusComponentInterface* api = orpheus_get_interface();
    PlaceholderState arena;
    void* state = NULL;
    OrpheusConfig config = {0};
    float data[] = {0.25f, -0.5f, 0.75f, -1.0f};
    float output[4] = {0};
    OrpheusBuffer input = {data, ORPHEUS_FORMAT_F32, 2, 2, 2, true};
    OrpheusBuffer result = {output, ORPHEUS_FORMAT_F32, 2, 2, 0, true};
    const OrpheusBuffer* inputs[] = {&input};
    OrpheusBuffer* outputs[] = {&result, NULL};
    OrpheusProcessContext ctx = {0};
    OrpheusValue routes = {.type = ORPHEUS_VALUE_STRING, .value.str = "0,-1"};
    OrpheusValue mode = {.type = ORPHEUS_VALUE_STRING, .value.str = "silence"};
    config.state_block = &arena;
    CHECK(api->create(&state, &config) == ORPHEUS_OK);
    CHECK(api->prepare(state, &config) == ORPHEUS_OK);
    CHECK(api->set_parameter(state, "routes", &routes) == ORPHEUS_OK);
    ctx.inputs = inputs;
    ctx.outputs = outputs;
    ctx.input_count = 1;
    ctx.output_count = 2;
    ctx.frame_count = 2;
    CHECK(api->process(state, &ctx) == ORPHEUS_OK);
    CHECK(memcmp(data, output, sizeof(data)) == 0);
    inputs[0] = NULL;
    CHECK(api->process(state, &ctx) == ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->set_parameter(state, "mode", &mode) == ORPHEUS_OK);
    CHECK(api->process(state, &ctx) == ORPHEUS_OK);
    CHECK(output[0] == 0.0f && output[3] == 0.0f);
    result.frame_capacity = 1;
    CHECK(api->process(state, &ctx) == ORPHEUS_ERR_INVALID_ARG);
    result.frame_capacity = 2;
    routes.value.str = "32";
    CHECK(api->set_parameter(state, "routes", &routes) == ORPHEUS_ERR_INVALID_ARG);
    routes.value.str = "0,";
    CHECK(api->set_parameter(state, "routes", &routes) == ORPHEUS_ERR_INVALID_ARG);
    routes.value.str = "";
    CHECK(api->set_parameter(state, "routes", &routes) == ORPHEUS_OK);
    mode.value.str = "passthrough";
    CHECK(api->set_parameter(state, "mode", &mode) == ORPHEUS_OK);
    ctx.output_count = 0;
    CHECK(api->process(state, &ctx) == ORPHEUS_OK);
    routes.value.str = "-1,-1";
    CHECK(api->set_parameter(state, "routes", &routes) == ORPHEUS_OK);
    ctx.input_count = 0;
    ctx.output_count = 2;
    CHECK(api->process(state, &ctx) == ORPHEUS_OK);
    CHECK(api->destroy(state) == ORPHEUS_OK);
    return 0;
}
