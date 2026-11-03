#ifndef ORPHEUS_COMPLEX_POWER_H
#define ORPHEUS_COMPLEX_POWER_H
#include "orpheus_abi.h"
typedef struct {
    uint32_t channels,bins,batches;
    float epsilon;
    bool owns_memory;
} ComplexPowerState;
#endif
