#include "orpheus_graph.h"
#include "orpheus_control.h"

#include <stdio.h>

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    if (orpheus_generated_init(48000, 128) != 0) return 2;
    int result = 0;
    for (int i = 0; i < 7; ++i) {
        if (orpheus_generated_process_task_default(128) != 0) {
            result = 3;
            goto teardown;
        }
    }
    float metrics[32];
    if (orpheus_control_get_bulk(argv[1], "metrics", metrics, 32) != 0) {
        result = 4;
        goto teardown;
    }
    printf("BULKVALUE %s metrics", argv[1]);
    for (int i = 0; i < 32; ++i) printf(" %.9g", metrics[i]);
    printf("\n");
teardown:
    orpheus_generated_teardown();
    return result;
}
