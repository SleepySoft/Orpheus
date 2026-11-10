#pragma once

typedef struct orpheus_builtin_biquad_AlgorithmState {
    float b0, b1, b2, a1, a2;
    float z1[32], z2[32], x1[32], x2[32], y1[32], y2[32];
} orpheus_builtin_biquad_AlgorithmState;
