#pragma once

typedef struct orpheus_builtin_ifft_AlgorithmState {
    float twiddleCos[512], twiddleSin[512], scratchR[1024], scratchI[1024];
} orpheus_builtin_ifft_AlgorithmState;
