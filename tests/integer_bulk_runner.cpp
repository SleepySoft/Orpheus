#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ORPHEUS_TEST_GENERATED
#include "orpheus_graph.h"
#include "orpheus_control.h"
#else
#include "orpheus_runtime/runtime.h"
static orpheus::Runtime* g_runtime;
#endif

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%d: %s\n", __LINE__, #expr); return 1; } } while (0)

static int binary_write(uint32_t id, const int32_t* values, size_t count, int valid) {
    uint8_t input[512], output[512];
    size_t output_size = 0;
    OrpheusMessageHeader header = {id, ORPHEUS_MSG_MAKE(ORPHEUS_MSG_CALL, 0, 71, count)};
    memcpy(input, &header, sizeof(header));
    memcpy(input + sizeof(header), values, count * sizeof(int32_t));
#ifdef ORPHEUS_TEST_GENERATED
    int rc = orpheus_control_message(input, sizeof(header) + count * 4,
                                    output, sizeof(output), &output_size);
#else
    int rc = g_runtime->message(input, sizeof(header) + count * 4,
                               output, sizeof(output), &output_size);
#endif
    CHECK(rc == ORPHEUS_OK && output_size >= sizeof(header));
    memcpy(&header, output, sizeof(header));
    CHECK(header.route_id == id && ORPHEUS_MSG_CALL_ID(&header) == 71);
    CHECK(ORPHEUS_MSG_TYPE(&header) == ORPHEUS_MSG_RESPONSE);
    CHECK(((ORPHEUS_MSG_FLAGS(&header) & ORPHEUS_MSG_FLAG_ERROR) == 0) == valid);
    return 0;
}

int main(int argc, char** argv) {
#ifdef ORPHEUS_TEST_GENERATED
    CHECK(argc >= 4 && orpheus_generated_init(48000, 32) == ORPHEUS_OK);
    const int first = 2;
    const char* mode = argv[1];
#define GET(n, k, v) orpheus_control_get_value(n, k, v)
#define SET(n, k, v) orpheus_control_set_value(n, k, v)
#define WRITE(n, v, c) orpheus_control_write_bulk(n, "delays_i32", v, c)
#define WRITE_ID(id, v, c) orpheus_control_write_bulk_id(id, v, c)
#define READ(n, v, c) orpheus_control_get_bulk(n, "delays_i32", v, c)
#define PROCESS() (strcmp(mode, "task") == 0 ? orpheus_generated_process_task_default(0) : orpheus_generated_process(32))
#else
    CHECK(argc >= 6);
    const orpheus::Plan plan = orpheus::Plan::load_from_file(argv[1]);
    orpheus::Runtime runtime;
    CHECK(runtime.load_plan(plan, argv[2]) == ORPHEUS_OK);
    g_runtime = &runtime;
    const int first = 4;
    const char* mode = argv[3];
#define GET(n, k, v) runtime.get_parameter(n, k, v)
#define SET(n, k, v) runtime.set_parameter(n, k, v)
#define WRITE(n, v, c) runtime.write_bulk(n, "delays_i32", v, c)
#define WRITE_ID(id, v, c) runtime.write_bulk_id(id, v, c)
#define READ(n, v, c) runtime.get_bulk(n, "delays_i32", v, c)
#define PROCESS() (strcmp(mode, "task") == 0 ? runtime.process_task("default", 0) : runtime.process_block(32))
#endif
    if (strcmp(mode, "control-global") == 0 || strcmp(mode, "control-task") == 0) {
        mode = strcmp(mode, "control-task") == 0 ? "task" : "global";
        const char* target = argv[first];
        int32_t actual[64], bad[64], good[64];
        OrpheusValue channels = {ORPHEUS_VALUE_INT, {0}};
        CHECK(GET(target, "channels", &channels) == ORPHEUS_OK);
        size_t count = (size_t)channels.value.i32;
        CHECK(PROCESS() == ORPHEUS_OK);
        CHECK(READ(target, actual, count) == ORPHEUS_OK && actual[0] == 0);
        CHECK(PROCESS() == ORPHEUS_OK);
        CHECK(READ(target, actual, count) == ORPHEUS_OK && actual[0] == 3);
        for (size_t c = 0; c < count; ++c) { bad[c] = 9; good[c] = 1; }
        CHECK(WRITE("control_source", bad, count) == ORPHEUS_OK);
        CHECK(PROCESS() != ORPHEUS_OK);
        CHECK(READ(target, actual, count) == ORPHEUS_OK && actual[0] == 3);
        CHECK(WRITE("control_source", good, count) == ORPHEUS_OK);
        CHECK(PROCESS() == ORPHEUS_OK);
        CHECK(READ(target, actual, count) == ORPHEUS_OK && actual[0] == 3);
        CHECK(PROCESS() == ORPHEUS_OK);
        CHECK(READ(target, actual, count) == ORPHEUS_OK);
        for (size_t c = 0; c < count; ++c) CHECK(actual[c] == 1);
    } else {
        const unsigned blocks[] = {65, 3, 7, 5, 4, 8};
        for (unsigned step = 0; step < 6; ++step) {
            for (int i = first; i + 1 < argc; i += 2) {
                const char* node = argv[i];
                uint32_t id = (uint32_t)strtoul(argv[i+1], NULL, 10);
                int32_t before[64], actual[64], expected[64], invalid[65];
                OrpheusValue channels = {ORPHEUS_VALUE_INT, {0}}, maximum = {ORPHEUS_VALUE_INT, {0}};
                CHECK(GET(node, "channels", &channels) == ORPHEUS_OK);
                CHECK(GET(node, "max_delay_samples", &maximum) == ORPHEUS_OK);
                size_t count = (size_t)channels.value.i32;
                CHECK(count > 0 && count <= 64 && READ(node, before, count) == ORPHEUS_OK);
                for (size_t c = 0; c < count; ++c) {
                    expected[c] = step == 1 ? (int32_t)((c*7+1) % (maximum.value.i32+1))
                        : step == 2 ? maximum.value.i32 - (int32_t)(c % (maximum.value.i32+1))
                        : step == 3 ? (int32_t)(c % 3) : (int32_t)((c+1) % (maximum.value.i32+1));
                    invalid[c] = 1;
                }
                invalid[count-1] = maximum.value.i32 + 1;
                CHECK(WRITE(node, invalid, count) != ORPHEUS_OK);
                CHECK(WRITE_ID(id, invalid, count) != ORPHEUS_OK);
                CHECK(binary_write(id, invalid, count, 0) == 0);
                CHECK(WRITE(node, invalid, count+1) != ORPHEUS_OK);
                CHECK(WRITE(node, NULL, count) != ORPHEUS_OK);
                if (step > 0 && step < 5) {
                    if (step == 1 || step == 4) CHECK(WRITE(node, expected, count) == ORPHEUS_OK);
                    if (step == 2) CHECK(WRITE_ID(id, expected, count) == ORPHEUS_OK);
                    if (step == 3) CHECK(binary_write(id, expected, count, 1) == 0);
                    CHECK(WRITE(node, invalid, count) != ORPHEUS_OK);
                }
                CHECK(READ(node, actual, count) == ORPHEUS_OK);
                CHECK(memcmp(before, actual, count * sizeof(int32_t)) == 0);
                if (step == 4 || step == 5) {
                    OrpheusValue value = {ORPHEUS_VALUE_INT, {0}};
                    value.value.i32 = step == 4 ? 2 : -1;
                    CHECK(SET(node, "delay_samples", value) == ORPHEUS_OK);
                }
            }
            for (unsigned block = 0; block < blocks[step]; ++block) CHECK(PROCESS() == ORPHEUS_OK);
        }
    }
#ifdef ORPHEUS_TEST_GENERATED
    orpheus_generated_teardown();
#endif
    return 0;
}
