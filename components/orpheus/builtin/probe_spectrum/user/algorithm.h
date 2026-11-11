#pragma once
#include <stdint.h>
typedef struct orpheus_builtin_probe_spectrum_AlgorithmState {
    uint32_t size;
    uint32_t half;
    uint32_t ring_pos;
    uint32_t samples_seen;
    float* window;
    float* ring;
    float* re;
    float* im;
    float* mags;
} orpheus_builtin_probe_spectrum_AlgorithmState;
