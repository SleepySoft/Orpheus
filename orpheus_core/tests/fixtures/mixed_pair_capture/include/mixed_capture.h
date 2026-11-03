#ifndef ORPHEUS_MIXED_PAIR_CAPTURE_H
#define ORPHEUS_MIXED_PAIR_CAPTURE_H
#include "orpheus_abi.h"
typedef struct {
    uint32_t channels,frames;
    int32_t calls,quantum;
    float packed_values[8192];
    int32_t decoded_values[8192];
    bool owns_memory;
} MixedPairCapture;
#endif
