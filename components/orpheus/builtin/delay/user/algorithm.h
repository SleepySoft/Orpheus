#pragma once

typedef struct orpheus_builtin_delay_AlgorithmState {
    float* buffer;
    uint32_t delay_samples, write_pos, capacity;
    float mix_smoothed, mix_coeff;
} orpheus_builtin_delay_AlgorithmState;
