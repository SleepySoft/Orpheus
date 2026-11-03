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

static float initial[8][2112];
static float zeros[2113];

int main(int argc, char** argv) {
#ifdef ORPHEUS_TEST_GENERATED
    CHECK(argc >= 6 && orpheus_generated_init(48000, 32) == ORPHEUS_OK);
    const int first = 2;
    const char* mode = argv[1];
#define WRITE(n, k, v, c) orpheus_control_write_bulk(n, k, v, c)
#define WRITE_ID(id, v, c) orpheus_control_write_bulk_id(id, v, c)
#define READ(n, k, v, c) orpheus_control_get_bulk(n, k, v, c)
#define PROCESS() (strcmp(mode, "task") == 0 ? orpheus_generated_process_task_default(0) : orpheus_generated_process(32))
#else
    CHECK(argc >= 8);
    orpheus::Runtime runtime;
    CHECK(runtime.load_plan(orpheus::Plan::load_from_file(argv[1]), argv[2]) == ORPHEUS_OK);
    const int first = 4;
    const char* mode = argv[3];
#define WRITE(n, k, v, c) runtime.write_bulk(n, k, v, c)
#define WRITE_ID(id, v, c) runtime.write_bulk_id(id, v, c)
#define READ(n, k, v, c) runtime.get_bulk(n, k, v, c)
#define PROCESS() (strcmp(mode, "task") == 0 ? runtime.process_task("default", 0) : runtime.process_block(32))
#endif
    CHECK((argc-first) % 4 == 0 && (argc-first)/4 <= 8);
    for (int i = first; i < argc; i += 4) {
        size_t count = (size_t)strtoul(argv[i+3], NULL, 10);
        CHECK(count > 0 && count <= 2112);
        CHECK(READ(argv[i], argv[i+2], initial[(i-first)/4], count) == ORPHEUS_OK);
    }
    const unsigned blocks[] = {2, 3, 1, 2, 1};
    for (unsigned step = 0; step < 5; ++step) {
        for (int i = first; i < argc; i += 4) {
            const char* node = argv[i];
            uint32_t id = (uint32_t)strtoul(argv[i+1], NULL, 10);
            const char* key = argv[i+2];
            size_t count = (size_t)strtoul(argv[i+3], NULL, 10);
            const float* next = step % 2 ? zeros : initial[(i-first)/4];
            float before[2112], after[2112];
            CHECK(READ(node, key, before, count) == ORPHEUS_OK);
            if (step % 2) CHECK(WRITE(node, key, next, count) == ORPHEUS_OK);
            else CHECK(WRITE_ID(id, next, count) == ORPHEUS_OK);
            CHECK(WRITE(node, key, zeros, count+1) != ORPHEUS_OK);
            CHECK(WRITE_ID(id, zeros, count+1) != ORPHEUS_OK);
            CHECK(READ(node, key, after, count) == ORPHEUS_OK);
            CHECK(memcmp(before, after, count*sizeof(float)) == 0);
        }
        for (unsigned block = 0; block < blocks[step]; ++block) CHECK(PROCESS() == ORPHEUS_OK);
        for (int i = first; i < argc; i += 4) {
            size_t count = (size_t)strtoul(argv[i+3], NULL, 10);
            float actual[2112];
            CHECK(READ(argv[i], argv[i+2], actual, count) == ORPHEUS_OK);
            CHECK(memcmp(actual, step % 2 ? zeros : initial[(i-first)/4], count*sizeof(float)) == 0);
        }
    }
#ifdef ORPHEUS_TEST_GENERATED
    orpheus_generated_teardown();
#endif
    return 0;
}
