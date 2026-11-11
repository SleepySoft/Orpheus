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
static float initial[4][32768];
static int32_t initial_indices[4][64];

int main(int argc, char** argv) {
#ifdef ORPHEUS_TEST_GENERATED
    CHECK(argc >= 5 && orpheus_generated_init(48000, 32) == ORPHEUS_OK);
    const int first = 2;
    const char* mode = argv[1];
#define GET(n, k, v) orpheus_control_get_value(n, k, v)
#define WRITE(n, k, v, c) orpheus_control_write_bulk(n, k, v, c)
#define WRITE_ID(id, v, c) orpheus_control_write_bulk_id(id, v, c)
#define READ(n, k, v, c) orpheus_control_get_bulk(n, k, v, c)
#define PROCESS() (strcmp(mode, "task") == 0 ? orpheus_generated_process_task_task_source(0) : orpheus_generated_process(32))
#else
    CHECK(argc >= 7);
    orpheus::Runtime runtime;
    CHECK(runtime.load_plan(orpheus::Plan::load_from_file(argv[1]), argv[2]) == ORPHEUS_OK);
    const int first = 4;
    const char* mode = argv[3];
#define GET(n, k, v) runtime.get_parameter(n, k, v)
#define WRITE(n, k, v, c) runtime.write_bulk(n, k, v, c)
#define WRITE_ID(id, v, c) runtime.write_bulk_id(id, v, c)
#define READ(n, k, v, c) runtime.get_bulk(n, k, v, c)
#define PROCESS() (strcmp(mode, "task") == 0 ? runtime.process_task("task_source", 0) : runtime.process_block(32))
#endif
    CHECK((argc-first) % 3 == 0 && (argc-first)/3 <= 4);
    const unsigned blocks[] = {8, 8, 1, 2, 1};
    for (unsigned step = 0; step < 5; ++step) {
        for (int i = first; i < argc; i += 3) {
            const char* node = argv[i];
            unsigned case_index = (unsigned)(i-first)/3;
            uint32_t index_id = (uint32_t)strtoul(argv[i+1], NULL, 10);
            uint32_t coefs_id = (uint32_t)strtoul(argv[i+2], NULL, 10);
            OrpheusValue channels = {ORPHEUS_VALUE_INT, {0}}, filters = channels, taps = channels;
            CHECK(GET(node, "channels", &channels) == ORPHEUS_OK);
            CHECK(GET(node, "num_filters", &filters) == ORPHEUS_OK);
            CHECK(GET(node, "num_taps", &taps) == ORPHEUS_OK);
            size_t count = (size_t)channels.value.i32;
            size_t coefs_count = (size_t)filters.value.i32*taps.value.i32;
            CHECK(count > 0 && count <= 64 && filters.value.i32 > 0 && coefs_count <= 32768);
            float coefficients[32768];
            int32_t before[64], actual[64], vector[64], bad[65] = {0};
            CHECK(READ(node, "filter_indices_i32", before, count) == ORPHEUS_OK);
            if (step == 0) {
                memcpy(initial_indices[case_index], before, count*sizeof(int32_t));
                CHECK(READ(node, "coefs_banked", initial[case_index], coefs_count) == ORPHEUS_OK);
            }
            for (size_t c = 0; c < count; ++c)
                vector[c] = (initial_indices[case_index][c]+(int32_t)step) % filters.value.i32;
            for (size_t c = 0; c < coefs_count; ++c)
                coefficients[c] = initial[case_index][c]*(step % 2 ? 0.5f : 1.0f);
            if (step % 2) {
                CHECK(WRITE(node, "filter_indices_i32", vector, count) == ORPHEUS_OK);
                CHECK(WRITE_ID(coefs_id, coefficients, coefs_count) == ORPHEUS_OK);
            } else {
                CHECK(WRITE_ID(index_id, vector, count) == ORPHEUS_OK);
                CHECK(WRITE(node, "coefs_banked", coefficients, coefs_count) == ORPHEUS_OK);
            }
            bad[count-1] = filters.value.i32;
            CHECK(WRITE(node, "filter_indices_i32", bad, count) != ORPHEUS_OK);
            bad[count-1] = -1;
            CHECK(WRITE_ID(index_id, bad, count) != ORPHEUS_OK);
            CHECK(WRITE(node, "filter_indices_i32", bad, count+1) != ORPHEUS_OK);
            CHECK(READ(node, "filter_indices_i32", actual, count) == ORPHEUS_OK);
            CHECK(memcmp(before, actual, count*sizeof(int32_t)) == 0);
        }
        for (unsigned n = 0; n < blocks[step]; ++n) CHECK(PROCESS() == ORPHEUS_OK);
    }
#ifdef ORPHEUS_TEST_GENERATED
    orpheus_generated_teardown();
#endif
    return 0;
}
