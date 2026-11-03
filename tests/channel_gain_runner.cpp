#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ORPHEUS_TEST_GENERATED
#include "orpheus_graph.h"
#include "orpheus_control.h"
#else
#include "orpheus_runtime/runtime.h"
#endif
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(int argc, char** argv) {
#ifdef ORPHEUS_TEST_GENERATED
    CHECK(argc >= 4 && orpheus_generated_init(48000, 32) == ORPHEUS_OK);
    int first = 2; const char* mode = argv[1];
#define GET(n, v) orpheus_control_get_value(n, "channels", v)
#define READ(n, v, c) orpheus_control_get_bulk(n, "gains", v, c)
#define WRITE(n, v, c) orpheus_control_write_bulk(n, "gains", v, c)
#define WRITE_ID(id, v, c) orpheus_control_write_bulk_id(id, v, c)
#define PROCESS() (strcmp(mode, "task") == 0 ? orpheus_generated_process_task_default(0) : orpheus_generated_process(32))
#else
    CHECK(argc >= 6);
    orpheus::Runtime runtime;
    CHECK(runtime.load_plan(orpheus::Plan::load_from_file(argv[1]), argv[2]) == ORPHEUS_OK);
    int first = 4; const char* mode = argv[3];
#define GET(n, v) runtime.get_parameter(n, "channels", v)
#define READ(n, v, c) runtime.get_bulk(n, "gains", v, c)
#define WRITE(n, v, c) runtime.write_bulk(n, "gains", v, c)
#define WRITE_ID(id, v, c) runtime.write_bulk_id(id, v, c)
#define PROCESS() (strcmp(mode, "task") == 0 ? runtime.process_task("default", 0) : runtime.process_block(32))
#endif
    float initial[4][32];
    const unsigned blocks[] = {2, 1, 2, 1};
    CHECK((argc-first) % 2 == 0 && (argc-first)/2 <= 4);
    for (unsigned step = 0; step < 4; ++step) {
        for (int i = first; i < argc; i += 2) {
            unsigned case_index = (unsigned)(i-first)/2;
            OrpheusValue channels = {ORPHEUS_VALUE_INT, {0}};
            CHECK(GET(argv[i], &channels) == ORPHEUS_OK && channels.value.i32 > 0 && channels.value.i32 <= 32);
            size_t count = (size_t)channels.value.i32;
            float before[32], actual[32], values[33] = {0};
            CHECK(READ(argv[i], before, count) == ORPHEUS_OK);
            if (!step) memcpy(initial[case_index], before, count*sizeof(float));
            else {
                for (size_t c = 0; c < count; ++c)
                    values[c] = step == 1 ? ((int)c % 5-2)/4.0f : step == 2 ? 0.0f : initial[case_index][c];
                uint32_t id = (uint32_t)strtoul(argv[i+1], NULL, 10);
                CHECK((step % 2 ? WRITE(argv[i], values, count) : WRITE_ID(id, values, count)) == ORPHEUS_OK);
                CHECK(WRITE(argv[i], values, count+1) != ORPHEUS_OK && WRITE_ID(id, values, count+1) != ORPHEUS_OK);
                CHECK(READ(argv[i], actual, count) == ORPHEUS_OK && !memcmp(before, actual, count*sizeof(float)));
            }
        }
        for (unsigned b = 0; b < blocks[step]; ++b) CHECK(PROCESS() == ORPHEUS_OK);
    }
#ifdef ORPHEUS_TEST_GENERATED
    orpheus_generated_teardown();
#endif
    return 0;
}
