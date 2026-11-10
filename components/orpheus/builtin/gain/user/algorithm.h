#pragma once

typedef struct orpheus_builtin_gain_AlgorithmState {
    float gain_linear;
    float target_linear;
    float smoothing_coeff;
    float linear_state;
    float per_channel_gains[32];
} orpheus_builtin_gain_AlgorithmState;
