#ifndef ORPHEUS_HALFCOMPLEX_DECODE_H
#define ORPHEUS_HALFCOMPLEX_DECODE_H
#include "orpheus_abi.h"
typedef struct {
    uint32_t channels, fft_size, batches, first_bin, bins;
    bool owns_memory;
} HalfcomplexDecodeState;
#endif
