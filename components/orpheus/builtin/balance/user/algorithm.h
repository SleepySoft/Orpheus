#pragma once

typedef struct orpheus_builtin_balance_AlgorithmState {
    float left_gain, right_gain;
    float target_left, target_right;
    float smoothing_coeff;
} orpheus_builtin_balance_AlgorithmState;
