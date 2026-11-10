#pragma once

typedef struct orpheus_builtin_exponential_gain_AlgorithmState {
    float delayed_target, filter_state;
    bool load_initial;
} orpheus_builtin_exponential_gain_AlgorithmState;
