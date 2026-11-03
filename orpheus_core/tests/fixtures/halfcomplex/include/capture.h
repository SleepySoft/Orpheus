#ifndef ORPHEUS_TEST_HALFCOMPLEX_CAPTURE_H
#define ORPHEUS_TEST_HALFCOMPLEX_CAPTURE_H
#include "orpheus_abi.h"
typedef struct {
    float packed_values[8192],decoded_values[8192];
    int32_t calls,quantum;
    unsigned channels,packed_frames,decoded_frames;
    bool owns_memory;
} HalfcomplexCapture;
#endif
