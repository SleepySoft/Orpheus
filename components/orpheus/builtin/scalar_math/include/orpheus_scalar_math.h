#ifndef ORPHEUS_SCALAR_MATH_H
#define ORPHEUS_SCALAR_MATH_H
#include "orpheus_abi.h"
typedef struct {
    float values[3];
    int32_t operation;
    bool owns_memory;
} ScalarMathState;
#endif
