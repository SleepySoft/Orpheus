#include <math.h>
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
    CHECK(argc >= 4);
    const char* mode = argv[1];
    const char* kind = argv[2];
    const int phase = atoi(argv[3]);
    CHECK(orpheus_generated_init(48000,
        strcmp(kind, "scalar") == 0 || strcmp(kind, "independent") == 0 ? 32 : 480) == ORPHEUS_OK);
#define GET(n, k, v) orpheus_control_get_value(n, k, v)
#define WRITE(n, v) orpheus_control_write_bulk(n, "values_storage", v, 4)
#define READ(n, v) orpheus_control_get_bulk(n, "values_storage", v, 4)
#define PROCESS() (strcmp(mode, "task") == 0 ? orpheus_generated_process_task_task_clock(0) : orpheus_generated_process(strcmp(kind, "scalar") == 0 || strcmp(kind, "independent") == 0 ? 32 : 480))
#ifdef ORPHEUS_TEST_SCOPE
#define OTHER() orpheus_generated_process_task_task_other(0)
#endif
#else
    CHECK(argc >= 6);
    orpheus::Runtime runtime;
    CHECK(runtime.load_plan(orpheus::Plan::load_from_file(argv[1]), argv[2]) == ORPHEUS_OK);
    const char* mode = argv[3];
    const char* kind = argv[4];
    const int phase = atoi(argv[5]);
#define GET(n, k, v) runtime.get_parameter(n, k, v)
#define WRITE(n, v) runtime.write_bulk(n, "values_storage", v, 4)
#define READ(n, v) runtime.get_bulk(n, "values_storage", v, 4)
#define PROCESS() (strcmp(mode, "task") == 0 ? runtime.process_task("task_clock", 0) : runtime.process_block(strcmp(kind, "scalar") == 0 || strcmp(kind, "independent") == 0 ? 32 : 480))
#define OTHER() runtime.process_task("task_other", 0)
#endif

    if (strcmp(kind, "scalar") == 0) {
        for (int step = 0; step < 4; ++step) {
            CHECK(PROCESS() == ORPHEUS_OK);
            OrpheusValue source = {ORPHEUS_VALUE_FLOAT, {0}}, target = {ORPHEUS_VALUE_FLOAT, {0}};
            CHECK(GET("rms", "rms", &source) == ORPHEUS_OK);
            CHECK(GET("gain", "gain_db", &target) == ORPHEUS_OK);
            CHECK(fabsf(source.value.f32 - 0.25f) < 1e-6f);
            CHECK(source.value.f32 == target.value.f32);
        }
    } else if (strcmp(kind, "independent") == 0) {
        for (int step = 0; step < 4; ++step) CHECK(PROCESS() == ORPHEUS_OK);
    } else if (strcmp(kind, "scope") == 0) {
#if !defined(ORPHEUS_TEST_GENERATED) || defined(ORPHEUS_TEST_SCOPE)
        const float input[4] = {9, 8, 7, 6};
        float actual[4] = {0};
        CHECK(WRITE("a", input) == ORPHEUS_OK);
        CHECK(OTHER() == ORPHEUS_OK);
        CHECK(READ("a", actual) == ORPHEUS_OK);
        CHECK(actual[0] == 1 && actual[3] == 4);
        CHECK(PROCESS() == ORPHEUS_OK);
        CHECK(READ("a", actual) == ORPHEUS_OK);
        CHECK(memcmp(input, actual, sizeof(input)) == 0);
#else
        CHECK(false);
#endif
    } else {
        float expected[4] = {0};
        for (int step = 0; step < 6; ++step) {
            const float input[4] = {(float)(step + 1), -2.0f, 3.5f, 4.0f};
            float actual[4] = {0};
            CHECK(WRITE("a", input) == ORPHEUS_OK);
            CHECK(PROCESS() == ORPHEUS_OK);
            if (step % 2 == phase) memcpy(expected, input, sizeof(input));
            CHECK(READ("b", actual) == ORPHEUS_OK);
            CHECK(memcmp(expected, actual, sizeof(expected)) == 0);
        }
    }
#ifdef ORPHEUS_TEST_GENERATED
    orpheus_generated_teardown();
#endif
    return 0;
}
