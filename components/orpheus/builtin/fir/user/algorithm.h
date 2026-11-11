#ifndef ORPHEUS_FIR_ALGORITHM_H
#define ORPHEUS_FIR_ALGORITHM_H
#include <stdint.h>
typedef struct {
    float* coeffs;
    float* delay;
    uint32_t* pos;
} orpheus_builtin_fir_AlgorithmState;
#endif
