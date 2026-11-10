#pragma once

typedef struct orpheus_builtin_delta_hold_AlgorithmState {
    float previous[64];
    uint32_t counter[64];
    int32_t active[64];
} orpheus_builtin_delta_hold_AlgorithmState;
