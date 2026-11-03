#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifdef ORPHEUS_TEST_GENERATED
#include "orpheus_graph.h"
#include "orpheus_control.h"
#else
#include "orpheus_runtime/runtime.h"
#endif
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(int argc, char** argv) {
#ifdef ORPHEUS_TEST_GENERATED
    CHECK(argc >= 5 && orpheus_generated_init(48000, 32) == ORPHEUS_OK);
    int first = 2; const char* mode = argv[1];
#define GET(n, v) orpheus_control_get_value(n, "channels_out", v)
#define ACCUMULATE(n, v) orpheus_control_get_value(n, "accumulate", v)
#define ENABLE(n, v) orpheus_control_set_value(n, "enabled", *(v))
#define READ(n, k, v, c) orpheus_control_get_bulk(n, k, v, c)
#define WRITE(n, k, v, c) orpheus_control_write_bulk(n, k, v, c)
#define WRITE_ID(id, v, c) orpheus_control_write_bulk_id(id, v, c)
#define PROCESS() (strcmp(mode, "task") == 0 ? orpheus_generated_process_task_default(0) : orpheus_generated_process(32))
#else
    CHECK(argc >= 7);
    orpheus::Runtime runtime;
    CHECK(runtime.load_plan(orpheus::Plan::load_from_file(argv[1]), argv[2]) == ORPHEUS_OK);
    int first = 4; const char* mode = argv[3];
#define GET(n, v) runtime.get_parameter(n, "channels_out", v)
#define ACCUMULATE(n, v) runtime.get_parameter(n, "accumulate", v)
#define ENABLE(n, v) runtime.set_parameter(n, "enabled", *(v))
#define READ(n, k, v, c) runtime.get_bulk(n, k, v, c)
#define WRITE(n, k, v, c) runtime.write_bulk(n, k, v, c)
#define WRITE_ID(id, v, c) runtime.write_bulk_id(id, v, c)
#define PROCESS() (strcmp(mode, "task") == 0 ? runtime.process_task("default", 0) : runtime.process_block(32))
#endif
    int32_t initial_indices[4][64];
    float initial_gains[4][64];
    const unsigned blocks[] = {2, 1, 2, 1};
    CHECK((argc-first) % 3 == 0 && (argc-first)/3 <= 4);
    for (unsigned step = 0; step < 4; ++step) {
        for (int i = first; i < argc; i += 3) {
            unsigned ci = (unsigned)(i-first)/3;
            OrpheusValue channels = {ORPHEUS_VALUE_INT, {0}};
            CHECK(GET(argv[i], &channels) == ORPHEUS_OK && channels.value.i32 > 0 && channels.value.i32 <= 64);
            OrpheusValue accumulate = {ORPHEUS_VALUE_BOOL, {0}}, enabled = {ORPHEUS_VALUE_BOOL, {0}};
            CHECK(ACCUMULATE(argv[i], &accumulate) == ORPHEUS_OK);
            enabled.value.b = step != 2;
            CHECK(ENABLE(argv[i], &enabled) == ORPHEUS_OK);
            size_t count = (size_t)channels.value.i32;
            int32_t before_indices[64], actual_indices[64], indices[65] = {0};
            float before_gains[64], actual_gains[64], gains[65] = {0};
            CHECK(READ(argv[i], "indices", before_indices, count) == ORPHEUS_OK);
            CHECK(READ(argv[i], "gains", before_gains, count) == ORPHEUS_OK);
            if (!step) {
                memcpy(initial_indices[ci], before_indices, count*sizeof(int32_t));
                memcpy(initial_gains[ci], before_gains, count*sizeof(float));
            } else {
                for (size_t c = 0; c < count; ++c) {
                    int32_t index = initial_indices[ci][c];
                    indices[c] = step == 1 ? (index == INT32_MAX ? index : index+1) :
                        step == 2 && !accumulate.value.b ? -1 : index;
                    gains[c] = step == 1 ? ((int)c % 5-2)/4.0f : step == 2 ? 0.0f : initial_gains[ci][c];
                }
                uint32_t index_id = (uint32_t)strtoul(argv[i+1], NULL, 10);
                uint32_t gain_id = (uint32_t)strtoul(argv[i+2], NULL, 10);
                CHECK((step % 2 ? WRITE(argv[i], "indices", indices, count) : WRITE_ID(index_id, indices, count)) == ORPHEUS_OK);
                CHECK((step % 2 ? WRITE_ID(gain_id, gains, count) : WRITE(argv[i], "gains", gains, count)) == ORPHEUS_OK);
                CHECK(WRITE(argv[i], "indices", indices, count+1) != ORPHEUS_OK);
                CHECK(WRITE_ID(index_id, indices, count+1) != ORPHEUS_OK);
                CHECK(WRITE(argv[i], "gains", gains, count+1) != ORPHEUS_OK);
                CHECK(WRITE_ID(gain_id, gains, count+1) != ORPHEUS_OK);
                CHECK(READ(argv[i], "indices", actual_indices, count) == ORPHEUS_OK &&
                      !memcmp(before_indices, actual_indices, count*sizeof(int32_t)));
                CHECK(READ(argv[i], "gains", actual_gains, count) == ORPHEUS_OK &&
                      !memcmp(before_gains, actual_gains, count*sizeof(float)));
            }
        }
        for (unsigned b = 0; b < blocks[step]; ++b) CHECK(PROCESS() == ORPHEUS_OK);
    }
#ifdef ORPHEUS_TEST_GENERATED
    orpheus_generated_teardown();
#endif
    return 0;
}
