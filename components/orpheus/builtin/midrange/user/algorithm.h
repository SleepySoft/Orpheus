#pragma once

typedef struct orpheus_builtin_midrange_AlgorithmState {
    float b0, a1, a2, z1[32], z2[32];
    float boost, target_boost, smoothing_coeff;
} orpheus_builtin_midrange_AlgorithmState;
