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
    CHECK(argc >= 6 && orpheus_generated_init(48000, 32) == ORPHEUS_OK);
    int first = 2;
    const char* mode = argv[1];
#define SET(n, k, v) orpheus_control_set_value(n, k, v)
#define SET_ID(id, v) orpheus_control_set_value_id(id, v)
#define GET(n, k, v) orpheus_control_get_value(n, k, v)
#define PROCESS() (strcmp(mode, "task") == 0 ? orpheus_generated_process_task_default(0) : orpheus_generated_process(32))
#else
    CHECK(argc >= 8);
    orpheus::Runtime runtime;
    CHECK(runtime.load_plan(orpheus::Plan::load_from_file(argv[1]), argv[2]) == ORPHEUS_OK);
    int first = 4;
    const char* mode = argv[3];
#define SET(n, k, v) runtime.set_parameter(n, k, v)
#define SET_ID(id, v) runtime.write_id(id, v)
#define GET(n, k, v) runtime.get_parameter(n, k, v)
#define PROCESS() (strcmp(mode, "task") == 0 ? runtime.process_task("default", 0) : runtime.process_block(32))
#endif
    const char* keys[] = {"target_gain", "ramp_coefficient", "remaining_blocks"};
    const float requests[][3] = {
        {0.25f, 0.98f, 3.0f}, {0.25f, 0.995f, 2.0f}, {0.8f, 1.015f, 2.0f},
        {0.2f, 0.99f, 0.0f}, {0.8f, 0.98f, 2.0f}, {0.8f, 1.0f, 1.0f},
    };
    const unsigned blocks[] = {2, 2, 1, 1, 1, 2};
    CHECK((argc-first) % 4 == 0);
    FILE* stream = fopen("gain_states.bin", "wb");
    CHECK(stream != NULL);
    for (unsigned step = 0; step < 6; ++step) {
        for (int i = first; i < argc; i += 4) {
            for (unsigned k = 0; k < 3; ++k) {
                uint32_t id = (uint32_t)strtoul(argv[i+k+1], NULL, 10);
                OrpheusValue v = {ORPHEUS_VALUE_FLOAT, {0}};
                v.value.f32 = requests[step][k];
                int result = step % 2 ? SET_ID(id, v) : SET(argv[i], keys[k], v);
                if (result != ORPHEUS_OK) {
                    fprintf(stderr, "更新失败：step=%u node=%s key=%s id=%u result=%d\n",
                            step, argv[i], keys[k], id, result);
#ifndef ORPHEUS_TEST_GENERATED
                    OrpheusResolvedData resolved;
                    int resolved_result = runtime.resolve(id, &resolved);
                    fprintf(stderr, "解析结果=%d node=%s key=%s kind=%u type=%u\n", resolved_result,
                            resolved.node ? resolved.node : "", resolved.key ? resolved.key : "",
                            (unsigned)resolved.kind, (unsigned)resolved.type);
#endif
                }
                CHECK(result == ORPHEUS_OK);
                OrpheusValue bad = {ORPHEUS_VALUE_FLOAT, {0}}, actual = {ORPHEUS_VALUE_FLOAT, {0}};
                bad.value.f32 = -1.0f;
                CHECK(SET(argv[i], keys[k], bad) != ORPHEUS_OK && SET_ID(id, bad) != ORPHEUS_OK);
                bad.type = ORPHEUS_VALUE_INT; bad.value.i32 = 1;
                CHECK(SET_ID(id, bad) != ORPHEUS_OK);
                CHECK(GET(argv[i], keys[k], &actual) == ORPHEUS_OK && actual.value.f32 == v.value.f32);
            }
        }
        for (unsigned b = 0; b < blocks[step]; ++b) {
            CHECK(PROCESS() == ORPHEUS_OK);
            for (int i = first; i < argc; i += 4) {
                const char* states[] = {"current_gain", "remaining_blocks"};
                for (unsigned k = 0; k < 2; ++k) {
                    OrpheusValue v = {ORPHEUS_VALUE_FLOAT, {0}};
                    CHECK(GET(argv[i], states[k], &v) == ORPHEUS_OK);
                    CHECK(fwrite(&v.value.f32, sizeof(float), 1, stream) == 1);
                }
            }
        }
    }
    CHECK(fclose(stream) == 0);
#ifdef ORPHEUS_TEST_GENERATED
    orpheus_generated_teardown();
#endif
    return 0;
}
