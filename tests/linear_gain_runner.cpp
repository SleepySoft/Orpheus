#include <math.h>
#include <stdio.h>
#include <string.h>
#ifdef ORPHEUS_TEST_GENERATED
#include "orpheus_graph.h"
#include "orpheus_control.h"
#else
#include "orpheus_runtime/runtime.h"
#endif

int main(int argc, char** argv) {
#ifdef ORPHEUS_TEST_GENERATED
    if (argc < 3 || orpheus_generated_init(48000, 32) != ORPHEUS_OK) return 1;
    const int first = 2;
    const char* mode = argv[1];
#define SET(n, k, v) orpheus_control_set_value(n, k, v)
#define GET(n, k, v) orpheus_control_get_value(n, k, v)
#else
    if (argc < 5) return 1;
    const orpheus::Plan plan = orpheus::Plan::load_from_file(argv[1]);
    orpheus::Runtime runtime;
    if (runtime.load_plan(plan, argv[2]) != ORPHEUS_OK) return 1;
    const int first = 4;
    const char* mode = argv[3];
#define SET(n, k, v) runtime.set_parameter(n, k, v)
#define GET(n, k, v) runtime.get_parameter(n, k, v)
#endif
    const unsigned blocks[] = {5, 3, 7, 5, 4};
    const float targets[] = {0.0f, -1.25f, 0.0f, 0.625f, -0.0f};
    for (unsigned step = 0; step < 5; ++step) {
        for (int i = first; i < argc; ++i) {
            OrpheusValue config = {ORPHEUS_VALUE_INT, {0}};
            if (GET(argv[i], "gain_mode", &config) != ORPHEUS_OK) return 1;
            if (config.value.i32 == 0) continue;
            OrpheusValue value = {ORPHEUS_VALUE_FLOAT, {0}};
            if (step) {
                value.value.f32 = targets[step];
                if (SET(argv[i], "gain_linear", value) != ORPHEUS_OK) return 1;
            }
            if (GET(argv[i], "gain_linear", &value) != ORPHEUS_OK) return 1;
            float previous = value.value.f32;
            value.value.f32 = NAN;
            if (SET(argv[i], "gain_linear", value) == ORPHEUS_OK) return 1;
            if (GET(argv[i], "gain_linear", &value) != ORPHEUS_OK ||
                memcmp(&previous, &value.value.f32, sizeof(float)) != 0) return 1;
            if (SET(argv[i], "gain_db", value) == ORPHEUS_OK) return 1;
            if (SET(argv[i], "linear_b0", value) == ORPHEUS_OK) return 1;
        }
        for (unsigned block = 0; block < blocks[step]; ++block) {
#ifdef ORPHEUS_TEST_GENERATED
            int rc = strcmp(mode, "task") == 0 ? orpheus_generated_process_task_default(0)
                : orpheus_generated_process(32);
#else
            int rc = strcmp(mode, "task") == 0 ? runtime.process_task("default", 0)
                : runtime.process_block(32);
#endif
            if (rc != ORPHEUS_OK) { fprintf(stderr, "增益执行错误：%d\n", rc); return 1; }
        }
    }
#ifdef ORPHEUS_TEST_GENERATED
    orpheus_generated_teardown();
#endif
    return 0;
}
