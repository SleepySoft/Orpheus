#ifndef ORPHEUS_RNC_MIMO_NLMS_ALGORITHM_H
#define ORPHEUS_RNC_MIMO_NLMS_ALGORITHM_H
#include <stdint.h>
typedef struct {
    float step_sizes[8];
    float weights[12000];
    float initial_weights[12000];
    float history[1500];
    uint32_t position;
    uint32_t leakage_reference;
    uint32_t leakage_output;
} orpheus_builtin_rnc_mimo_nlms_AlgorithmState;
#endif
