#include "component_state.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); return 1; } } while (0)
static int emissions;
static int emit(void* user, uint32_t port, const OrpheusDelivery* delivery) {
    (void)user;
    if (port != 1 || !delivery->buffer || delivery->frame_index != 17) return ORPHEUS_ERR_INVALID_ARG;
    ++emissions;
    return ORPHEUS_ERR_UNSUPPORTED;
}
int main(void) {
    const OrpheusComponentInterface* api = orpheus_get_interface();
    orpheus_builtin_channel_router_State state;
    const char* keys[] = {"channels_in", "channels_out", "indices"};
    OrpheusValue values[] = {
        {.type=ORPHEUS_VALUE_INT, .value.i32=3}, {.type=ORPHEUS_VALUE_INT, .value.i32=6},
        {.type=ORPHEUS_VALUE_STRING, .value.str="-2147483648, -1, 0, 2, 3, 2147483647"}
    };
    OrpheusConfig config = {.state_block=&state, .param_ids=keys, .param_values=values, .param_count=3};
    void* instance = NULL;
    CHECK(api->create(&instance, &config) == ORPHEUS_OK && instance == &state);
    CHECK(api->prepare(instance, &config) == ORPHEUS_OK);
    uint32_t data[] = {0x7fa12345, 0x80000000, 0x7f800000, 1, 0xff800000, 0x7fc76543};
    uint32_t result[12], expected[] = {0, 0, 0x7fa12345, 0x7f800000, 0, 0,
                                     0, 0, 1, 0x7fc76543, 0, 0};
    OrpheusBuffer input = {.data=data, .channels=3, .format=ORPHEUS_FORMAT_F32,
        .frame_count=2, .frame_capacity=2, .interleaved=true};
    OrpheusBuffer output = {.data=result, .channels=6, .format=ORPHEUS_FORMAT_F32,
        .frame_capacity=2, .interleaved=true};
    const OrpheusBuffer* inputs[] = {&input};
    OrpheusBuffer* outputs[] = {&output};
    OrpheusProcessContext ctx = {.inputs=inputs, .outputs=outputs, .input_count=1,
        .output_count=1, .frame_count=2, .frame_index=17};
    CHECK(api->process(instance, &ctx) == ORPHEUS_OK && !memcmp(result, expected, sizeof(result)));
    CHECK(api->reset(instance) == ORPHEUS_OK && api->process(instance, &ctx) == ORPHEUS_OK);
    CHECK(!memcmp(result, expected, sizeof(result)));
    OrpheusContinuation continuation = {.emit=emit};
    ctx.continuation = &continuation;
    CHECK(api->process(instance, &ctx) == ORPHEUS_ERR_UNSUPPORTED && emissions == 1);
    ctx.frame_count = 0;
    CHECK(api->process(instance, &ctx) == ORPHEUS_OK && emissions == 1);
    ctx.frame_count = 2; ctx.continuation = NULL;
    const char* invalid[] = {"0", "0,1,2,3,4,", "0,1,2,3,4,5,6", "0,1,,3,4,5",
        "0,1,2,3,4,5x", "0,1,2,3,4,2147483648", "0,1,2,3,4,-2147483649",
        "0,1,2,3,4,1.5", "0,1,2,3,4,999999999999999999999999"};
    for (unsigned i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
        values[2].value.str = invalid[i];
        CHECK(api->prepare(instance, &config) == ORPHEUS_ERR_INVALID_ARG);
        CHECK(api->process(instance, &ctx) == ORPHEUS_OK && !memcmp(result, expected, sizeof(result)));
    }
    output.data = data;
    CHECK(api->process(instance, &ctx) == ORPHEUS_ERR_INVALID_ARG);
    output.data = result;
    input.frame_count = 1;
    CHECK(api->process(instance, &ctx) == ORPHEUS_ERR_INVALID_ARG);
    input.frame_count = 2;
    input.format = ORPHEUS_FORMAT_I32;
    CHECK(api->process(instance, &ctx) == ORPHEUS_ERR_INVALID_ARG);
    input.format = ORPHEUS_FORMAT_F32;
    outputs[0] = NULL;
    CHECK(api->process(instance, &ctx) == ORPHEUS_OK);
    inputs[0] = NULL;
    CHECK(api->process(instance, &ctx) == ORPHEUS_ERR_INVALID_ARG);
    inputs[0] = &input; outputs[0] = &output;
    values[2].value.str = "";
    CHECK(api->prepare(instance, &config) == ORPHEUS_OK);
    uint32_t identity[] = {data[0], data[1], data[2], 0, 0, 0, data[3], data[4], data[5], 0, 0, 0};
    CHECK(api->process(instance, &ctx) == ORPHEUS_OK && !memcmp(result, identity, sizeof(result)));
    values[0].value.i32 = values[1].value.i32 = 64;
    CHECK(api->prepare(instance, &config) == ORPHEUS_OK);
    uint32_t maximum[128], copied[128];
    for (unsigned i = 0; i < 128; ++i) maximum[i] = data[i%6];
    input.channels = output.channels = 64;
    input.data = maximum; output.data = copied;
    CHECK(api->process(instance, &ctx) == ORPHEUS_OK && !memcmp(maximum, copied, sizeof(maximum)));
    values[0].value.i32 = 65;
    CHECK(api->prepare(instance, &config) == ORPHEUS_ERR_INVALID_ARG);
    CHECK(api->destroy(instance) == ORPHEUS_OK);
    return 0;
}
