#pragma once
#include <stdint.h>

typedef struct orpheus_builtin_indexed_tone_AlgorithmState {
    float boost_coefs[5],cut_coefs[5],z1[64],z2[64];
    float current_gain,target_gain,final_target_gain,ramp_coeff,sample_rate;
    uint32_t frame_count,block_size;
    int32_t crossing_phase;
} orpheus_builtin_indexed_tone_AlgorithmState;
