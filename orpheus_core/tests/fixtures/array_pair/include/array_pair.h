#ifndef ORPHEUS_TEST_ARRAY_PAIR_H
#define ORPHEUS_TEST_ARRAY_PAIR_H
#include "orpheus_abi.h"
typedef struct {
    float packed_values[8192],decoded_values[8192];
    uint32_t channels,elements,mode;
    int32_t calls,quantum;
    bool owns_memory;
} ArrayPairState;
#endif
