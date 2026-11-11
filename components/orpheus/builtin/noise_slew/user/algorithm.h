#pragma once
typedef struct orpheus_builtin_noise_slew_AlgorithmState {
    double sample_rate;
    float rise_delta;
    float fall_delta;
    float prev[32];
} orpheus_builtin_noise_slew_AlgorithmState;
