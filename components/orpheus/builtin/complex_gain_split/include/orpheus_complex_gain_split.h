#ifndef ORPHEUS_COMPLEX_GAIN_SPLIT_H
#define ORPHEUS_COMPLEX_GAIN_SPLIT_H
#include "orpheus_abi.h"
typedef struct {
    uint32_t channels,elements;
    bool owns_memory;
} ComplexGainSplitState;
#endif
