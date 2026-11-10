#pragma once

typedef struct orpheus_builtin_mute_AlgorithmState {
    float gain_linear, target_linear, smoothing_coeff;
} orpheus_builtin_mute_AlgorithmState;
