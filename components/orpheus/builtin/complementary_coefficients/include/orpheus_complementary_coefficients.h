#ifndef ORPHEUS_COMPLEMENTARY_COEFFICIENTS_H
#define ORPHEUS_COMPLEMENTARY_COEFFICIENTS_H
#include "orpheus_abi.h"
typedef struct {
    uint32_t channels,elements;
    float total;
    bool owns_memory;
} ComplementaryCoefficientsState;
#endif
