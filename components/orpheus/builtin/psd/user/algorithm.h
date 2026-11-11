#pragma once

typedef struct orpheus_builtin_psd_AlgorithmState {
    uint32_t half;
    size_t magnitude_count;
    float* mags;
    float* re;
    float* im;
} orpheus_builtin_psd_AlgorithmState;
