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
    const int first = 2;
    const char* mode = argv[1];
#define SET(n, v) orpheus_control_set_value(n, "processing_enabled", v)
#define SET_ID(id, v) orpheus_control_set_value_id(id, v)
#define GET(n, v) orpheus_control_get_value(n, "processing_enabled", v)
#define PROCESS() (strcmp(mode, "task") == 0 ? orpheus_generated_process_task_default(0) : orpheus_generated_process(32))
#else
    CHECK(argc >= 6);
    orpheus::Runtime runtime;
    CHECK(runtime.load_plan(orpheus::Plan::load_from_file(argv[1]), argv[2]) == ORPHEUS_OK);
    const int first = 4;
    const char* mode = argv[3];
#define SET(n, v) runtime.set_parameter(n, "processing_enabled", v)
#define SET_ID(id, v) runtime.write_id(id, v)
#define GET(n, v) runtime.get_parameter(n, "processing_enabled", v)
#define PROCESS() (strcmp(mode, "task") == 0 ? runtime.process_task("default", 0) : runtime.process_block(32))
#endif
    const unsigned blocks[] = {2, 3, 1, 2, 1};
    for (unsigned step = 0; step < 5; ++step) {
        for (int i = first; i + 1 < argc; i += 2) {
            const char* node = argv[i];
            uint32_t id = (uint32_t)strtoul(argv[i+1], NULL, 10);
            OrpheusValue value = {ORPHEUS_VALUE_INT, {0}};
            value.value.i32 = step % 2 ? 0 : 1;
            if (step % 2) CHECK(SET_ID(id, value) == ORPHEUS_OK);
            else CHECK(SET(node, value) == ORPHEUS_OK);
            OrpheusValue bad = {ORPHEUS_VALUE_INT, {0}}, actual = {ORPHEUS_VALUE_INT, {0}};
            bad.value.i32 = 2;
            CHECK(SET(node, bad) != ORPHEUS_OK && SET_ID(id, bad) != ORPHEUS_OK);
            bad.type = ORPHEUS_VALUE_FLOAT; bad.value.f32 = 0.0f;
            CHECK(SET_ID(id, bad) != ORPHEUS_OK);
            CHECK(GET(node, &actual) == ORPHEUS_OK && actual.value.i32 == value.value.i32);
        }
        for (unsigned block = 0; block < blocks[step]; ++block) CHECK(PROCESS() == ORPHEUS_OK);
    }
#ifdef ORPHEUS_TEST_GENERATED
    orpheus_generated_teardown();
#endif
    return 0;
}
