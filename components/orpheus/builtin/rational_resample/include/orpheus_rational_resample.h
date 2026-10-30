#ifndef ORPHEUS_RATIONAL_RESAMPLE_H
#define ORPHEUS_RATIONAL_RESAMPLE_H

#include "orpheus_abi.h"

typedef struct {
    uint32_t channels;
    uint32_t up;
    uint32_t down;
    uint32_t taps;
    uint32_t cursor;
    float* coefficients;
    float* history;
} RationalResampleState;

#endif
