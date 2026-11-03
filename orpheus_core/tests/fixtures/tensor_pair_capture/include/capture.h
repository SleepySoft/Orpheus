#ifndef ORPHEUS_TEST_TENSOR_PAIR_CAPTURE_H
#define ORPHEUS_TEST_TENSOR_PAIR_CAPTURE_H
#include "orpheus_abi.h"
typedef struct {
    float packed_values[8192],decoded_values[8192];
    uint32_t channels_a,channels_b,frames_a,frames_b;
    int32_t calls,quantum;
    bool owns_memory;
} TensorPairCapture;
#endif
