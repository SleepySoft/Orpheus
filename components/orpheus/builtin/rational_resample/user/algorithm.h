#ifndef ORPHEUS_BUILTIN_RATIONAL_RESAMPLE_ALGORITHM_H
#define ORPHEUS_BUILTIN_RATIONAL_RESAMPLE_ALGORITHM_H

#include "orpheus_abi.h"

typedef struct {
    uint32_t cursor;
    float* coefficients;
    float* history;
} orpheus_builtin_rational_resample_AlgorithmState;

#endif
