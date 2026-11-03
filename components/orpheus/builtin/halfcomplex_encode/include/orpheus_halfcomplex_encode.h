#ifndef ORPHEUS_HALFCOMPLEX_ENCODE_H
#define ORPHEUS_HALFCOMPLEX_ENCODE_H
#include "orpheus_abi.h"
typedef struct {
    uint32_t channels,fft_size,batches,bins;
    bool owns_memory;
} HalfcomplexEncodeState;
#endif
