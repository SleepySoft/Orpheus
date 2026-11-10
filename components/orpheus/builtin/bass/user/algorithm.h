#pragma once

typedef struct orpheus_builtin_bass_AlgorithmState {
    float a1, z[32], boost, target_boost, smoothing_coeff;
} orpheus_builtin_bass_AlgorithmState;
