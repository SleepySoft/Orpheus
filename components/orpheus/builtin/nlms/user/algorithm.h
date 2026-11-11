#ifndef ORPHEUS_NLMS_ALGORITHM_H
#define ORPHEUS_NLMS_ALGORITHM_H
#include <stdint.h>
typedef struct {
    float* w;
    float* x;
    uint32_t* pos;
} orpheus_builtin_nlms_AlgorithmState;
#endif
