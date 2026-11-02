#ifndef ORPHEUS_TEST_SPECTRAL_GATE_H
#define ORPHEUS_TEST_SPECTRAL_GATE_H
#include "orpheus_abi.h"

typedef struct {
    uint32_t channels;
    uint32_t fft_size;
    uint32_t num_frames;
    uint32_t quantum;
    uint64_t calls;
} SpectralGateState;
#endif
