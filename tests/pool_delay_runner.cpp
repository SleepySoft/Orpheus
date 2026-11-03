#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ORPHEUS_TEST_GENERATED
#include "orpheus_graph.h"
#include "orpheus_control.h"
#else
#include "orpheus_runtime/runtime.h"
#endif

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%d: %s\n", __LINE__, #expr); return 1; } } while (0)

int main(int argc, char** argv) {
#ifdef ORPHEUS_TEST_GENERATED
    CHECK(argc >= 4 && orpheus_generated_init(48000, 32) == ORPHEUS_OK);
    int first = 2;
    const char* mode = argv[1];
#define GET(n, k, v) orpheus_control_get_value(n, k, v)
#define WRITE(n, v, c) orpheus_control_write_bulk(n, "delays_i32", v, c)
#define WRITE_ID(id, v, c) orpheus_control_write_bulk_id(id, v, c)
#define READ(n, v, c) orpheus_control_get_bulk(n, "delays_i32", v, c)
#define PROCESS() (strcmp(mode, "task") == 0 ? orpheus_generated_process_task_default(0) : orpheus_generated_process(32))
#else
    CHECK(argc >= 6);
    orpheus::Runtime runtime;
    CHECK(runtime.load_plan(orpheus::Plan::load_from_file(argv[1]), argv[2]) == ORPHEUS_OK);
    int first = 4;
    const char* mode = argv[3];
#define GET(n, k, v) runtime.get_parameter(n, k, v)
#define WRITE(n, v, c) runtime.write_bulk(n, "delays_i32", v, c)
#define WRITE_ID(id, v, c) runtime.write_bulk_id(id, v, c)
#define READ(n, v, c) runtime.get_bulk(n, "delays_i32", v, c)
#define PROCESS() (strcmp(mode, "task") == 0 ? runtime.process_task("default", 0) : runtime.process_block(32))
#endif
    const unsigned blocks[] = {1, 1, 1, 30, 2, 4};
    for (unsigned step = 0; step < 6; ++step) {
        for (int i = first; i + 1 < argc; i += 2) {
            const char* node = argv[i];
            uint32_t id = (uint32_t)strtoul(argv[i+1], NULL, 10);
            OrpheusValue channels = {ORPHEUS_VALUE_INT, {0}}, capacity = {ORPHEUS_VALUE_INT, {0}};
            CHECK(GET(node, "channels", &channels) == ORPHEUS_OK);
            CHECK(GET(node, "pool_size_samples", &capacity) == ORPHEUS_OK);
            size_t count = (size_t)channels.value.i32;
            CHECK(count > 0 && count <= 64);
            int32_t before[64], actual[64], vector[64], bad[65] = {0};
            CHECK(READ(node, before, count) == ORPHEUS_OK);
            for (size_t c = 0; c < count; ++c) {
                vector[c] = step == 1 ? (int32_t)(c+2) % (capacity.value.i32+1)
                    : step == 2 ? (int32_t)(c == 0 ? 4 : c+2) % (capacity.value.i32+1)
                    : step == 3 ? (int32_t)(c*7+33) % (capacity.value.i32+1)
                    : step == 4 ? 0 : capacity.value.i32;
            }
            if (step > 0) {
                if (step % 2) CHECK(WRITE(node, vector, count) == ORPHEUS_OK);
                else CHECK(WRITE_ID(id, vector, count) == ORPHEUS_OK);
            }
            bad[count-1] = capacity.value.i32 + 1;
            CHECK(WRITE(node, bad, count) != ORPHEUS_OK);
            bad[count-1] = -1;
            CHECK(WRITE_ID(id, bad, count) != ORPHEUS_OK);
            CHECK(WRITE(node, bad, count+1) != ORPHEUS_OK);
            CHECK(READ(node, actual, count) == ORPHEUS_OK);
            CHECK(memcmp(before, actual, count * sizeof(int32_t)) == 0);
        }
        for (unsigned n = 0; n < blocks[step]; ++n) CHECK(PROCESS() == ORPHEUS_OK);
    }
#ifdef ORPHEUS_TEST_GENERATED
    orpheus_generated_teardown();
#endif
    return 0;
}
