#ifndef ORPHEUS_FADE_ALGORITHM_H
#define ORPHEUS_FADE_ALGORITHM_H
#include <stdint.h>
typedef struct {
    float a1,z[32],front_gain,back_gain,target_front,target_back,smoothing_coeff;
    int32_t chan_map[32];
    float current_gains[4],target_gains[4],coefficients[4];
    int32_t remaining_blocks[4];
} orpheus_builtin_fade_AlgorithmState;
#endif
