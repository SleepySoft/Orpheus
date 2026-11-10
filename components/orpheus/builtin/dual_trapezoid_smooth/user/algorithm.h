#pragma once

typedef struct orpheus_builtin_dual_trapezoid_smooth_AlgorithmState {
    float* history;
    size_t count;
    bool initialized;
} orpheus_builtin_dual_trapezoid_smooth_AlgorithmState;
